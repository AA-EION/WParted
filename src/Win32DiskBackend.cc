/* Copyright (C) 2026 WParted contributors
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/* Win32DiskBackend
 *
 * DiskBackend implementation using the Windows partition manager:
 *   SetupDi (GUID_DEVINTERFACE_DISK)        disk enumeration and identity
 *   IOCTL_STORAGE_QUERY_PROPERTY            model, serial, bus, sector sizes
 *   IOCTL_DISK_GET_DRIVE_LAYOUT_EX          read partition table
 *   IOCTL_DISK_CREATE_DISK                  new partition table
 *   IOCTL_DISK_SET_DRIVE_LAYOUT_EX          write partition table
 *   IOCTL_DISK_UPDATE_PROPERTIES            make Windows re-read the table
 *   FSCTL_LOCK_VOLUME/FSCTL_DISMOUNT_VOLUME exclusive access before changes
 */

#include "WinDiskBackend.h"

#include <cstring>
#include <mutex>

#if defined(_WIN32)

#include "Win32Volumes.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winioctl.h>
#include <setupapi.h>
#include <initguid.h>
#include <devguid.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <set>

#ifndef IOCTL_DISK_GET_DISK_ATTRIBUTES
#define IOCTL_DISK_GET_DISK_ATTRIBUTES CTL_CODE(IOCTL_DISK_BASE, 0x003c, METHOD_BUFFERED, FILE_ANY_ACCESS)
typedef struct _GET_DISK_ATTRIBUTES {
	DWORD     Version;
	DWORD     Reserved1;
	DWORDLONG Attributes;
} GET_DISK_ATTRIBUTES;
#endif
#ifndef DISK_ATTRIBUTE_OFFLINE
#define DISK_ATTRIBUTE_OFFLINE 0x0000000000000001ULL
#endif
#ifndef DISK_ATTRIBUTE_READ_ONLY
#define DISK_ATTRIBUTE_READ_ONLY 0x0000000000000002ULL
#endif

// GUID_DEVINTERFACE_DISK {53F56307-B6BF-11D0-94F2-00A0C91EFB8B}
DEFINE_GUID(WPARTED_GUID_DEVINTERFACE_DISK, 0x53f56307L, 0xb6bf, 0x11d0, 0x94, 0xf2, 0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b);


namespace WParted
{
namespace WinDisk
{

namespace
{

using Win32Volumes::to_utf8;
using Win32Volumes::last_error_message;


class Handle
{
public:
	explicit Handle(HANDLE h = INVALID_HANDLE_VALUE) : m_h(h) {}
	~Handle() { if (valid()) CloseHandle(m_h); }
	Handle(const Handle&) = delete;
	Handle& operator=(const Handle&) = delete;
	bool valid() const { return m_h != INVALID_HANDLE_VALUE && m_h != nullptr; }
	HANDLE get() const { return m_h; }
private:
	HANDLE m_h;
};


// Page aligned buffer for raw disk I/O.
class AlignedBuffer
{
public:
	explicit AlignedBuffer(size_t size) : m_size(size)
	{
		m_p = VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	}
	~AlignedBuffer() { if (m_p) VirtualFree(m_p, 0, MEM_RELEASE); }
	AlignedBuffer(const AlignedBuffer&) = delete;
	AlignedBuffer& operator=(const AlignedBuffer&) = delete;
	void* get() const { return m_p; }
	size_t size() const { return m_size; }
private:
	void*  m_p;
	size_t m_size;
};


std::wstring physical_drive(int index)
{
	return L"\\\\.\\PhysicalDrive" + std::to_wstring(index);
}


HANDLE open_disk(int index, DWORD access)
{
	return CreateFileW(physical_drive(index).c_str(), access, FILE_SHARE_READ | FILE_SHARE_WRITE,
	                   nullptr, OPEN_EXISTING, 0, nullptr);
}


GUID to_win(const Guid& g)
{
	GUID w;
	w.Data1 = g.data1;
	w.Data2 = g.data2;
	w.Data3 = g.data3;
	std::memcpy(w.Data4, g.data4, 8);
	return w;
}


Guid from_win(const GUID& w)
{
	Guid g;
	g.data1 = w.Data1;
	g.data2 = w.Data2;
	g.data3 = w.Data3;
	std::memcpy(g.data4, w.Data4, 8);
	return g;
}


std::string trimmed(const char* base, size_t buffer_size, DWORD offset)
{
	if (offset == 0 || offset >= buffer_size)
		return std::string();
	std::string s;
	for (size_t i = offset; i < buffer_size && base[i]; i++)
		s.push_back(base[i]);
	size_t a = s.find_first_not_of(' ');
	size_t b = s.find_last_not_of(' ');
	return (a == std::string::npos) ? std::string() : s.substr(a, b - a + 1);
}


std::string bus_name(STORAGE_BUS_TYPE bus)
{
	switch (static_cast<int>(bus))
	{
		case 0x01: return "SCSI";
		case 0x02: return "ATAPI";
		case 0x03: return "ATA";
		case 0x04: return "IEEE 1394";
		case 0x05: return "SSA";
		case 0x06: return "Fibre";
		case 0x07: return "USB";
		case 0x08: return "RAID";
		case 0x09: return "iSCSI";
		case 0x0A: return "SAS";
		case 0x0B: return "SATA";
		case 0x0C: return "SD";
		case 0x0D: return "MMC";
		case 0x0E: return "Virtual";
		case 0x0F: return "File Backed Virtual";
		case 0x10: return "Spaces";
		case 0x11: return "NVMe";
		case 0x12: return "SCM";
		case 0x13: return "UFS";
		default:   return "Unknown";
	}
}


// Disk number -> PnP device instance id for all present disks.
std::map<int, std::string> disk_instances()
{
	std::map<int, std::string> out;
	HDEVINFO info = SetupDiGetClassDevsW(&WPARTED_GUID_DEVINTERFACE_DISK, nullptr, nullptr,
	                                     DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
	if (info == INVALID_HANDLE_VALUE)
		return out;

	SP_DEVICE_INTERFACE_DATA iface;
	iface.cbSize = sizeof(iface);
	for (DWORD i = 0; SetupDiEnumDeviceInterfaces(info, nullptr, &WPARTED_GUID_DEVINTERFACE_DISK, i, &iface); i++)
	{
		DWORD needed = 0;
		SetupDiGetDeviceInterfaceDetailW(info, &iface, nullptr, 0, &needed, nullptr);
		if (needed == 0)
			continue;
		std::vector<BYTE> buf(needed);
		SP_DEVICE_INTERFACE_DETAIL_DATA_W* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buf.data());
		detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
		SP_DEVINFO_DATA devinfo;
		devinfo.cbSize = sizeof(devinfo);
		if (! SetupDiGetDeviceInterfaceDetailW(info, &iface, detail, needed, nullptr, &devinfo))
			continue;

		Handle h(CreateFileW(detail->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
		                     OPEN_EXISTING, 0, nullptr));
		if (! h.valid())
			continue;
		STORAGE_DEVICE_NUMBER number;
		DWORD returned = 0;
		if (! DeviceIoControl(h.get(), IOCTL_STORAGE_GET_DEVICE_NUMBER, nullptr, 0, &number,
		                      sizeof(number), &returned, nullptr) ||
		    number.DeviceType != FILE_DEVICE_DISK)
			continue;

		wchar_t instance[1024] = {0};
		std::string id;
		if (SetupDiGetDeviceInstanceIdW(info, &devinfo, instance, 1024, nullptr))
			id = to_utf8(instance);
		out[static_cast<int>(number.DeviceNumber)] = id;
	}
	SetupDiDestroyDeviceInfoList(info);
	return out;
}


class Win32Session : public DiskSession
{
public:
	Win32Session(int index, uint64_t offset, uint64_t length, uint32_t sector_size, bool writable,
	             bool lock_volumes);
	~Win32Session() override;
	bool open(std::string& error);

	uint64_t length() const override       { return m_length; }
	uint32_t sector_size() const override  { return m_sector_size; }
	bool writable() const override         { return m_writable; }
	bool read(uint64_t offset, uint64_t length, void* buffer, std::string& error) override;
	bool write(uint64_t offset, uint64_t length, const void* buffer, std::string& error) override;
	bool flush(std::string& error) override;

private:
	bool in_range(uint64_t offset, uint64_t length, std::string& error) const;

	int      m_index;
	uint64_t m_offset;
	uint64_t m_length;
	uint32_t m_sector_size;
	bool     m_writable;
	bool     m_lock_volumes;
	HANDLE   m_handle = INVALID_HANDLE_VALUE;
	std::vector<std::unique_ptr<Win32Volumes::Lock>> m_locks;
};


class Win32DiskBackend : public DiskBackend
{
public:
	std::vector<int> enumerate_disks() override
	{
		std::vector<int> out;
		for (const auto& kv : disk_instances())
			out.push_back(kv.first);
		return out;
	}


	bool query_disk(int index, DiskInfo& info, std::string& error) override
	{
		info = DiskInfo();
		info.index = index;

		std::map<int, std::string> instances = disk_instances();
		auto inst = instances.find(index);
		if (inst == instances.end())
		{
			error = "No such disk";
			return false;
		}
		info.instance_id = inst->second;

		Handle h(open_disk(index, GENERIC_READ));
		if (! h.valid())
		{
			error = "Unable to open " + to_utf8(physical_drive(index)) + ": " + last_error_message(GetLastError());
			return false;
		}

		DWORD returned = 0;
		DISK_GEOMETRY_EX geo;
		if (! DeviceIoControl(h.get(), IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, nullptr, 0, &geo, sizeof(geo),
		                      &returned, nullptr))
		{
			error = "Unable to read the disk geometry: " + last_error_message(GetLastError());
			return false;
		}
		info.logical_sector_size = geo.Geometry.BytesPerSector;
		info.size_bytes = static_cast<uint64_t>(geo.DiskSize.QuadPart);
		GET_LENGTH_INFORMATION len;
		if (DeviceIoControl(h.get(), IOCTL_DISK_GET_LENGTH_INFO, nullptr, 0, &len, sizeof(len), &returned, nullptr))
			info.size_bytes = static_cast<uint64_t>(len.Length.QuadPart);
		info.physical_sector_size = info.logical_sector_size;

		STORAGE_PROPERTY_QUERY query;
		std::memset(&query, 0, sizeof(query));
		query.PropertyId = StorageDeviceProperty;
		query.QueryType  = PropertyStandardQuery;
		std::vector<char> desc(4096, 0);
		if (DeviceIoControl(h.get(), IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), desc.data(),
		                    static_cast<DWORD>(desc.size()), &returned, nullptr) &&
		    returned >= sizeof(STORAGE_DEVICE_DESCRIPTOR))
		{
			const STORAGE_DEVICE_DESCRIPTOR* d = reinterpret_cast<const STORAGE_DEVICE_DESCRIPTOR*>(desc.data());
			size_t n = std::min<size_t>(returned, desc.size());
			info.vendor    = trimmed(desc.data(), n, d->VendorIdOffset);
			info.model     = trimmed(desc.data(), n, d->ProductIdOffset);
			info.serial    = trimmed(desc.data(), n, d->SerialNumberOffset);
			info.bus       = bus_name(d->BusType);
			info.removable = d->RemovableMedia != FALSE;
		}

		std::memset(&query, 0, sizeof(query));
		query.PropertyId = StorageAccessAlignmentProperty;
		query.QueryType  = PropertyStandardQuery;
		STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR align;
		std::memset(&align, 0, sizeof(align));
		if (DeviceIoControl(h.get(), IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), &align,
		                    sizeof(align), &returned, nullptr) && align.BytesPerPhysicalSector)
			info.physical_sector_size = align.BytesPerPhysicalSector;

		GET_DISK_ATTRIBUTES attrs;
		std::memset(&attrs, 0, sizeof(attrs));
		if (DeviceIoControl(h.get(), IOCTL_DISK_GET_DISK_ATTRIBUTES, nullptr, 0, &attrs, sizeof(attrs),
		                    &returned, nullptr))
		{
			if (attrs.Attributes & DISK_ATTRIBUTE_OFFLINE)
			{
				info.offline = true;
				info.read_only_reason = "The disk is offline in Windows.  Bring it online in Disk "
				                        "Management to modify it.";
			}
			if (attrs.Attributes & DISK_ATTRIBUTE_READ_ONLY)
			{
				info.read_only = true;
				info.read_only_reason = "The disk has the Windows read-only attribute set.";
			}
		}
		if (! DeviceIoControl(h.get(), IOCTL_DISK_IS_WRITABLE, nullptr, 0, nullptr, 0, &returned, nullptr) &&
		    GetLastError() == ERROR_WRITE_PROTECT)
		{
			info.read_only = true;
			info.read_only_reason = "The disk is write protected.";
		}

		for (const Win32Volumes::Volume& v : Win32Volumes::enumerate())
			if (v.system)
				for (const Win32Volumes::Extent& e : v.extents)
					if (static_cast<int>(e.disk) == index)
						info.system_disk = true;

		if (info.logical_sector_size < 512 || info.size_bytes == 0)
		{
			error = "The disk reports an invalid size";
			return false;
		}
		return true;
	}


	bool read_layout(int index, DiskLayout& layout, std::string& error) override
	{
		layout = DiskLayout();
		Handle h(open_disk(index, GENERIC_READ));
		if (! h.valid())
		{
			error = last_error_message(GetLastError());
			return false;
		}

		DWORD entries = 128;
		std::vector<BYTE> buf;
		for (;;)
		{
			buf.assign(sizeof(DRIVE_LAYOUT_INFORMATION_EX) + entries * sizeof(PARTITION_INFORMATION_EX), 0);
			DWORD returned = 0;
			if (DeviceIoControl(h.get(), IOCTL_DISK_GET_DRIVE_LAYOUT_EX, nullptr, 0, buf.data(),
			                    static_cast<DWORD>(buf.size()), &returned, nullptr))
				break;
			DWORD err = GetLastError();
			if (err != ERROR_INSUFFICIENT_BUFFER || entries >= 65536)
			{
				error = last_error_message(err);
				return false;
			}
			entries *= 4;
		}

		const DRIVE_LAYOUT_INFORMATION_EX* dl = reinterpret_cast<const DRIVE_LAYOUT_INFORMATION_EX*>(buf.data());
		switch (dl->PartitionStyle)
		{
			case PARTITION_STYLE_MBR:
				layout.style = TableStyle::MBR;
				layout.mbr_signature = dl->Mbr.Signature;
				break;
			case PARTITION_STYLE_GPT:
				layout.style = TableStyle::GPT;
				layout.gpt_disk_id = from_win(dl->Gpt.DiskId);
				layout.gpt_first_usable_offset = static_cast<uint64_t>(dl->Gpt.StartingUsableOffset.QuadPart);
				layout.gpt_usable_length = static_cast<uint64_t>(dl->Gpt.UsableLength.QuadPart);
				layout.gpt_max_entries = dl->Gpt.MaxPartitionCount;
				break;
			default:
				layout.style = TableStyle::RAW;
				return true;
		}

		for (DWORD i = 0; i < dl->PartitionCount; i++)
		{
			const PARTITION_INFORMATION_EX& p = dl->PartitionEntry[i];
			LayoutEntry e;
			e.offset  = static_cast<uint64_t>(p.StartingOffset.QuadPart);
			e.length  = static_cast<uint64_t>(p.PartitionLength.QuadPart);
			e.number  = p.PartitionNumber;
			e.rewrite = p.RewritePartition != FALSE;
			if (layout.style == TableStyle::MBR)
			{
				e.mbr_type           = p.Mbr.PartitionType;
				e.mbr_boot           = p.Mbr.BootIndicator != FALSE;
				e.mbr_recognized     = p.Mbr.RecognizedPartition != FALSE;
				e.mbr_hidden_sectors = p.Mbr.HiddenSectors;
			}
			else
			{
				e.gpt_type       = from_win(p.Gpt.PartitionType);
				e.gpt_id         = from_win(p.Gpt.PartitionId);
				e.gpt_attributes = p.Gpt.Attributes;
				for (int k = 0; k < 36 && p.Gpt.Name[k]; k++)
					e.gpt_name.push_back(static_cast<char16_t>(p.Gpt.Name[k]));
			}
			layout.entries.push_back(e);
		}
		return true;
	}


	bool read_sectors(int index, uint64_t lba, uint64_t count, uint32_t sector_size,
	                  void* buffer, std::string& error) override
	{
		Handle h(open_disk(index, GENERIC_READ));
		if (! h.valid())
		{
			error = last_error_message(GetLastError());
			return false;
		}
		return transfer(h.get(), lba, count, sector_size, buffer, false, error);
	}


	bool write_sectors(int index, uint64_t lba, uint64_t count, uint32_t sector_size,
	                   const void* buffer, std::string& error) override
	{
		const uint64_t offset = lba * sector_size;
		const uint64_t length = count * sector_size;

		// Only inside one existing partition, or anywhere on a disk without any
		// partition table.
		DiskLayout layout;
		if (! read_layout(index, layout, error))
			return false;
		if (layout.style != TableStyle::RAW)
		{
			bool inside = false;
			for (const LayoutEntry& e : layout.entries)
			{
				if (e.length == 0)
					continue;
				if (layout.style == TableStyle::MBR &&
				    (e.mbr_type == MbrType::EMPTY || MbrType::is_extended(e.mbr_type)))
					continue;
				if (layout.style == TableStyle::GPT && e.gpt_type.is_null())
					continue;
				if (offset >= e.offset && offset + length <= e.offset + e.length)
					inside = true;
			}
			if (! inside)
			{
				error = "refusing to write outside of a single partition";
				return false;
			}
		}

		std::vector<std::unique_ptr<Win32Volumes::Lock>> locks;
		Win32Volumes::Extent range;
		range.disk = static_cast<uint32_t>(index);
		range.offset = offset;
		range.length = length;
		if (! Win32Volumes::lock_ranges(static_cast<uint32_t>(index), {range}, locks, error))
			return false;

		Handle h(open_disk(index, GENERIC_READ | GENERIC_WRITE));
		if (! h.valid())
		{
			error = last_error_message(GetLastError());
			return false;
		}
		bool ok = transfer(h.get(), lba, count, sector_size, const_cast<void*>(buffer), true, error);
		if (ok && ! FlushFileBuffers(h.get()))
		{
			error = "flush failed: " + last_error_message(GetLastError());
			ok = false;
		}
		return ok;
	}


	std::unique_ptr<DiskSession> open_session(int index, uint64_t offset, uint64_t length,
	                                          bool writable, SessionRange range,
	                                          std::string& error) override
	{
		return open_win32_session(index, offset, length, writable, writable, range, error);
	}


	std::unique_ptr<DiskSession> open_locked_reader(int index, uint64_t offset, uint64_t length,
	                                                std::string& error) override
	{
		return open_win32_session(index, offset, length, false, true, SessionRange::INSIDE_PARTITION, error);
	}


	std::unique_ptr<DiskSession> open_win32_session(int index, uint64_t offset, uint64_t length,
	                                                bool writable, bool lock_volumes, SessionRange range,
	                                                std::string& error)
	{
		DiskInfo info;
		DiskLayout layout;
		if (! query_disk(index, info, error) || ! read_layout(index, layout, error))
			return nullptr;
		if (writable && (info.read_only || info.offline))
		{
			error = "the disk is read-only";
			return nullptr;
		}
		if (! session_range_allowed(layout, info.logical_sector_size, info.size_bytes, offset, length,
		                            range, error))
			return nullptr;

		std::unique_ptr<Win32Session> session(new Win32Session(index, offset, length,
		                                                        info.logical_sector_size, writable,
		                                                        lock_volumes));
		if (! session->open(error))
			return nullptr;
		return std::unique_ptr<DiskSession>(session.release());
	}


	bool write_layout(int index, const DiskLayout& current, const DiskLayout& layout,
	                  bool new_table, std::string& error) override
	{
		if (layout.style == TableStyle::RAW)
		{
			error = "refusing to write an empty (RAW) layout";
			return false;
		}

		// Volumes on partitions which disappear or change must be locked first.
		std::vector<Win32Volumes::Extent> ranges;
		std::vector<Extent> changed;
		if (new_table)
		{
			for (const LayoutEntry& e : current.entries)
			{
				if (e.length == 0)
					continue;
				Extent x;
				x.offset = e.offset;
				x.length = e.length;
				changed.push_back(x);
			}
		}
		else
		{
			changed = changed_extents(current, layout);
		}
		for (const Extent& c : changed)
		{
			Win32Volumes::Extent r;
			r.disk   = static_cast<uint32_t>(index);
			r.offset = c.offset;
			r.length = c.length;
			ranges.push_back(r);
		}
		std::vector<std::unique_ptr<Win32Volumes::Lock>> locks;
		if (! Win32Volumes::lock_ranges(static_cast<uint32_t>(index), ranges, locks, error))
			return false;

		Handle h(open_disk(index, GENERIC_READ | GENERIC_WRITE));
		if (! h.valid())
		{
			error = last_error_message(GetLastError());
			return false;
		}
		DWORD returned = 0;

		bool any_partition = false;
		for (const LayoutEntry& e : layout.entries)
			any_partition = any_partition || e.length > 0;

		if (new_table)
		{
			CREATE_DISK cd;
			std::memset(&cd, 0, sizeof(cd));
			if (layout.style == TableStyle::GPT)
			{
				cd.PartitionStyle = PARTITION_STYLE_GPT;
				cd.Gpt.DiskId = to_win(layout.gpt_disk_id);
				cd.Gpt.MaxPartitionCount = layout.gpt_max_entries ? layout.gpt_max_entries : 128;
			}
			else
			{
				cd.PartitionStyle = PARTITION_STYLE_MBR;
				cd.Mbr.Signature = layout.mbr_signature;
			}
			if (! DeviceIoControl(h.get(), IOCTL_DISK_CREATE_DISK, &cd, sizeof(cd), nullptr, 0, &returned, nullptr))
			{
				error = "IOCTL_DISK_CREATE_DISK failed: " + last_error_message(GetLastError());
				return false;
			}
			DeviceIoControl(h.get(), IOCTL_DISK_UPDATE_PROPERTIES, nullptr, 0, nullptr, 0, &returned, nullptr);
			if (! any_partition)
				return true;
		}

		size_t count = layout.entries.size();
		size_t size = sizeof(DRIVE_LAYOUT_INFORMATION_EX) + (count ? count : 1) * sizeof(PARTITION_INFORMATION_EX);
		std::vector<BYTE> buf(size, 0);
		DRIVE_LAYOUT_INFORMATION_EX* dl = reinterpret_cast<DRIVE_LAYOUT_INFORMATION_EX*>(buf.data());
		dl->PartitionCount = static_cast<DWORD>(count);
		if (layout.style == TableStyle::GPT)
		{
			dl->PartitionStyle = PARTITION_STYLE_GPT;
			if (new_table)
			{
				// Use what Windows just created for the usable area.
				DiskLayout created;
				if (read_layout(index, created, error) && created.style == TableStyle::GPT)
				{
					dl->Gpt.StartingUsableOffset.QuadPart = static_cast<LONGLONG>(created.gpt_first_usable_offset);
					dl->Gpt.UsableLength.QuadPart = static_cast<LONGLONG>(created.gpt_usable_length);
				}
			}
			else
			{
				dl->Gpt.StartingUsableOffset.QuadPart = static_cast<LONGLONG>(layout.gpt_first_usable_offset);
				dl->Gpt.UsableLength.QuadPart = static_cast<LONGLONG>(layout.gpt_usable_length);
			}
			dl->Gpt.DiskId = to_win(layout.gpt_disk_id);
			dl->Gpt.MaxPartitionCount = layout.gpt_max_entries ? layout.gpt_max_entries : 128;
		}
		else
		{
			dl->PartitionStyle = PARTITION_STYLE_MBR;
			dl->Mbr.Signature = layout.mbr_signature;
		}
		for (size_t i = 0; i < count; i++)
		{
			const LayoutEntry& e = layout.entries[i];
			PARTITION_INFORMATION_EX& p = dl->PartitionEntry[i];
			p.PartitionStyle = (layout.style == TableStyle::GPT) ? PARTITION_STYLE_GPT : PARTITION_STYLE_MBR;
			p.StartingOffset.QuadPart  = static_cast<LONGLONG>(e.offset);
			p.PartitionLength.QuadPart = static_cast<LONGLONG>(e.length);
			p.PartitionNumber          = e.number;
			p.RewritePartition         = TRUE;
			if (layout.style == TableStyle::GPT)
			{
				p.Gpt.PartitionType = to_win(e.gpt_type);
				p.Gpt.PartitionId   = to_win(e.gpt_id);
				p.Gpt.Attributes    = e.gpt_attributes;
				for (size_t k = 0; k < 36; k++)
					p.Gpt.Name[k] = (k < e.gpt_name.size()) ? static_cast<WCHAR>(e.gpt_name[k]) : 0;
			}
			else
			{
				p.Mbr.PartitionType       = e.mbr_type;
				p.Mbr.BootIndicator       = e.mbr_boot ? TRUE : FALSE;
				p.Mbr.RecognizedPartition = e.mbr_recognized ? TRUE : FALSE;
				p.Mbr.HiddenSectors       = e.mbr_hidden_sectors;
			}
		}
		if (! DeviceIoControl(h.get(), IOCTL_DISK_SET_DRIVE_LAYOUT_EX, buf.data(), static_cast<DWORD>(size),
		                      nullptr, 0, &returned, nullptr))
		{
			error = "IOCTL_DISK_SET_DRIVE_LAYOUT_EX failed: " + last_error_message(GetLastError());
			return false;
		}
		DeviceIoControl(h.get(), IOCTL_DISK_UPDATE_PROPERTIES, nullptr, 0, nullptr, 0, &returned, nullptr);
		return true;
	}


	bool update_properties(int index, std::string& error) override
	{
		Handle h(open_disk(index, GENERIC_READ | GENERIC_WRITE));
		if (! h.valid())
		{
			error = last_error_message(GetLastError());
			return false;
		}
		DWORD returned = 0;
		if (! DeviceIoControl(h.get(), IOCTL_DISK_UPDATE_PROPERTIES, nullptr, 0, nullptr, 0, &returned, nullptr))
		{
			error = last_error_message(GetLastError());
			return false;
		}
		return true;
	}


public:
	static bool transfer(HANDLE h, uint64_t lba, uint64_t count, uint32_t sector_size, void* buffer,
	                     bool write, std::string& error)
	{
		const size_t chunk = 1024 * 1024;
		AlignedBuffer bounce(chunk);
		if (! bounce.get())
		{
			error = "out of memory";
			return false;
		}
		uint64_t total = count * sector_size;
		uint64_t done = 0;
		uint8_t* user = static_cast<uint8_t*>(buffer);
		while (done < total)
		{
			DWORD n = static_cast<DWORD>(std::min<uint64_t>(chunk, total - done));
			LARGE_INTEGER pos;
			pos.QuadPart = static_cast<LONGLONG>(lba * sector_size + done);
			if (! SetFilePointerEx(h, pos, nullptr, FILE_BEGIN))
			{
				error = "seek failed: " + last_error_message(GetLastError());
				return false;
			}
			DWORD moved = 0;
			if (write)
			{
				std::memcpy(bounce.get(), user + done, n);
				if (! WriteFile(h, bounce.get(), n, &moved, nullptr) || moved != n)
				{
					error = "write failed: " + last_error_message(GetLastError());
					return false;
				}
			}
			else
			{
				if (! ReadFile(h, bounce.get(), n, &moved, nullptr) || moved != n)
				{
					error = "read failed: " + last_error_message(GetLastError());
					return false;
				}
				std::memcpy(user + done, bounce.get(), n);
			}
			done += n;
		}
		return true;
	}
};

// Opened by Win32DiskBackend::open_session() after the range has been checked
// against the partition table.  Writable sessions keep every Windows volume on
// the range locked and dismounted until the session is destroyed.
Win32Session::Win32Session(int index, uint64_t offset, uint64_t length, uint32_t sector_size,
                           bool writable, bool lock_volumes)
 : m_index(index), m_offset(offset), m_length(length), m_sector_size(sector_size),
   m_writable(writable), m_lock_volumes(writable || lock_volumes)
{
}


Win32Session::~Win32Session()
{
	std::string error;
	if (m_handle != INVALID_HANDLE_VALUE)
	{
		if (m_writable)
			FlushFileBuffers(m_handle);
		CloseHandle(m_handle);
	}
	m_locks.clear();
}


bool Win32Session::open(std::string& error)
{
	if (m_lock_volumes)
	{
		Win32Volumes::Extent range;
		range.disk   = static_cast<uint32_t>(m_index);
		range.offset = m_offset;
		range.length = m_length;
		if (! Win32Volumes::lock_ranges(static_cast<uint32_t>(m_index), {range}, m_locks, error))
			return false;
	}
	m_handle = open_disk(m_index, GENERIC_READ | (m_writable ? GENERIC_WRITE : 0));
	if (m_handle == INVALID_HANDLE_VALUE)
	{
		error = "Unable to open " + to_utf8(physical_drive(m_index)) + ": " + last_error_message(GetLastError());
		m_locks.clear();
		return false;
	}
	return true;
}


bool Win32Session::in_range(uint64_t offset, uint64_t length, std::string& error) const
{
	if (length == 0 || offset > m_length || length > m_length - offset)
	{
		error = "access outside of the session range";
		return false;
	}
	return true;
}


bool Win32Session::read(uint64_t offset, uint64_t length, void* buffer, std::string& error)
{
	if (! in_range(offset, length, error))
		return false;
	uint64_t first = offset / m_sector_size;
	uint64_t last  = (offset + length + m_sector_size - 1) / m_sector_size;
	uint64_t lba   = m_offset / m_sector_size + first;
	if (offset % m_sector_size == 0 && length % m_sector_size == 0)
		return Win32DiskBackend::transfer(m_handle, lba, last - first, m_sector_size, buffer, false, error);
	std::vector<uint8_t> tmp((last - first) * m_sector_size);
	if (! Win32DiskBackend::transfer(m_handle, lba, last - first, m_sector_size, tmp.data(), false, error))
		return false;
	std::memcpy(buffer, tmp.data() + (offset - first * m_sector_size), length);
	return true;
}


bool Win32Session::write(uint64_t offset, uint64_t length, const void* buffer, std::string& error)
{
	if (! m_writable)
	{
		error = "session is read-only";
		return false;
	}
	if (! in_range(offset, length, error))
		return false;
	if (offset % m_sector_size != 0 || length % m_sector_size != 0)
	{
		error = "unaligned write";
		return false;
	}
	return Win32DiskBackend::transfer(m_handle, (m_offset + offset) / m_sector_size, length / m_sector_size,
	                                  m_sector_size, const_cast<void*>(buffer), true, error);
}


bool Win32Session::flush(std::string& error)
{
	if (m_writable && ! FlushFileBuffers(m_handle))
	{
		error = "flush failed: " + last_error_message(GetLastError());
		return false;
	}
	return true;
}


}  // anonymous namespace


static DiskBackend* create_default_backend()
{
	static Win32DiskBackend backend;
	return &backend;
}

}  // namespace WinDisk
}  // namespace WParted

#else  /* ! _WIN32 */

namespace WParted
{
namespace WinDisk
{
static DiskBackend* create_default_backend()
{
	return nullptr;
}
}  // namespace WinDisk
}  // namespace WParted

#endif /* _WIN32 */


namespace WParted
{
namespace WinDisk
{

namespace
{

// Session on top of read_sectors()/write_sectors(), which validate every write
// themselves.
class ForwardingSession : public DiskSession
{
public:
	ForwardingSession(DiskBackend* backend, int index, uint64_t offset, uint64_t length,
	                  uint32_t sector_size, bool writable)
	 : m_backend(backend), m_index(index), m_offset(offset), m_length(length),
	   m_sector_size(sector_size), m_writable(writable)
	{}

	uint64_t length() const override       { return m_length; }
	uint32_t sector_size() const override  { return m_sector_size; }
	bool writable() const override         { return m_writable; }

	bool read(uint64_t offset, uint64_t length, void* buffer, std::string& error) override
	{
		if (! in_range(offset, length, error))
			return false;
		// Read the covering whole sectors and copy out the requested bytes.
		uint64_t first = offset / m_sector_size;
		uint64_t last  = (offset + length + m_sector_size - 1) / m_sector_size;
		if (offset % m_sector_size == 0 && length % m_sector_size == 0)
			return m_backend->read_sectors(m_index, m_offset / m_sector_size + first, last - first,
			                               m_sector_size, buffer, error);
		std::vector<uint8_t> tmp((last - first) * m_sector_size);
		if (! m_backend->read_sectors(m_index, m_offset / m_sector_size + first, last - first,
		                              m_sector_size, tmp.data(), error))
			return false;
		std::memcpy(buffer, tmp.data() + (offset - first * m_sector_size), length);
		return true;
	}

	bool write(uint64_t offset, uint64_t length, const void* buffer, std::string& error) override
	{
		if (! m_writable)
		{
			error = "session is read-only";
			return false;
		}
		if (! in_range(offset, length, error))
			return false;
		if (offset % m_sector_size != 0 || length % m_sector_size != 0)
		{
			error = "unaligned write";
			return false;
		}
		return m_backend->write_sectors(m_index, (m_offset + offset) / m_sector_size, length / m_sector_size,
		                                m_sector_size, buffer, error);
	}

	bool flush(std::string& error) override
	{
		return true;
	}

private:
	bool in_range(uint64_t offset, uint64_t length, std::string& error) const
	{
		if (length == 0 || offset > m_length || length > m_length - offset)
		{
			error = "access outside of the session range";
			return false;
		}
		return true;
	}

	DiskBackend* m_backend;
	int          m_index;
	uint64_t     m_offset;
	uint64_t     m_length;
	uint32_t     m_sector_size;
	bool         m_writable;
};

}  // anonymous namespace


std::unique_ptr<DiskSession> DiskBackend::open_locked_reader(int index, uint64_t offset, uint64_t length,
                                                             std::string& error)
{
	return open_session(index, offset, length, false, SessionRange::INSIDE_PARTITION, error);
}


std::unique_ptr<DiskSession> DiskBackend::open_session(int index, uint64_t offset, uint64_t length,
                                                       bool writable, SessionRange range,
                                                       std::string& error)
{
	DiskInfo info;
	DiskLayout layout;
	if (! query_disk(index, info, error) || ! read_layout(index, layout, error))
		return nullptr;
	if (! session_range_allowed(layout, info.logical_sector_size, info.size_bytes, offset, length,
	                            range == SessionRange::INSIDE_PARTITION ? range
	                                                                    : SessionRange::PARTITION_AND_FREE_SPACE,
	                            error))
		return nullptr;
	if (range != SessionRange::INSIDE_PARTITION)
	{
		// write_sectors() would refuse the free space part.
		error = "this disk backend does not support move sessions";
		return nullptr;
	}
	return std::unique_ptr<DiskSession>(new ForwardingSession(this, index, offset, length,
	                                                          info.logical_sector_size, writable));
}


static std::mutex   g_backend_mutex;
static DiskBackend* g_backend = nullptr;
static bool         g_backend_set = false;

DiskBackend* get_backend()
{
	std::lock_guard<std::mutex> lock(g_backend_mutex);
	if (! g_backend_set)
	{
		g_backend = create_default_backend();
		g_backend_set = true;
	}
	return g_backend;
}

void set_backend(DiskBackend* backend)
{
	std::lock_guard<std::mutex> lock(g_backend_mutex);
	g_backend = backend;
	g_backend_set = true;
}

}  // namespace WinDisk
}  // namespace WParted
