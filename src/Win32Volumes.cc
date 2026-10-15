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

#if defined(_WIN32)

#include "Win32Volumes.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winioctl.h>

#include <chrono>
#include <thread>


namespace WParted
{
namespace Win32Volumes
{


std::string to_utf8(const std::wstring& s)
{
	if (s.empty())
		return std::string();
	int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
	std::string out(n > 0 ? n : 0, '\0');
	if (n > 0)
		WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &out[0], n, nullptr, nullptr);
	return out;
}


std::wstring from_utf8(const std::string& s)
{
	if (s.empty())
		return std::wstring();
	int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
	std::wstring out(n > 0 ? n : 0, L'\0');
	if (n > 0)
		MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &out[0], n);
	return out;
}


std::string last_error_message(unsigned long code)
{
	wchar_t* buf = nullptr;
	DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
	                         FORMAT_MESSAGE_IGNORE_INSERTS,
	                         nullptr, code, 0, reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
	std::wstring text = (n && buf) ? std::wstring(buf, n) : std::wstring();
	if (buf)
		LocalFree(buf);
	while (! text.empty() && (text.back() == L'\n' || text.back() == L'\r' || text.back() == L' '))
		text.pop_back();
	return to_utf8(text) + " (error " + std::to_string(code) + ")";
}


std::wstring device_path(const Volume& volume)
{
	std::wstring p = volume.guid_path;
	if (! p.empty() && p.back() == L'\\')
		p.pop_back();
	return p;
}


std::string Volume::busy_reason() const
{
	if (system)
		return "it holds the running Windows installation or its boot manager";
	if (paging)
		return "it holds an active page, swap or hibernation file";
	if (! mount_points.empty())
	{
		std::string mps;
		for (const std::wstring& mp : mount_points)
			mps += (mps.empty() ? "" : ", ") + to_utf8(mp);
		return "it is mounted at " + mps;
	}
	return "";
}


bool Volume::on_disk_range(uint32_t d, uint64_t off, uint64_t len) const
{
	for (const Extent& e : extents)
		if (e.disk == d && e.offset < off + len && off < e.offset + e.length)
			return true;
	return false;
}


bool Volume::exactly(uint32_t d, uint64_t off, uint64_t len) const
{
	return extents.size() == 1 && extents[0].disk == d && extents[0].offset == off &&
	       extents[0].length == len;
}


static std::wstring volume_nt_device(const std::wstring& guid_path)
{
	// "\\?\Volume{GUID}\" -> "Volume{GUID}" -> "\Device\HarddiskVolumeN"
	std::wstring name = guid_path;
	if (name.compare(0, 4, L"\\\\?\\") == 0)
		name = name.substr(4);
	if (! name.empty() && name.back() == L'\\')
		name.pop_back();
	wchar_t target[MAX_PATH] = {0};
	if (QueryDosDeviceW(name.c_str(), target, MAX_PATH) == 0)
		return std::wstring();
	return target;
}


static std::wstring system_volume_guid()
{
	wchar_t windir[MAX_PATH] = {0};
	wchar_t root[MAX_PATH] = {0};
	wchar_t guid[MAX_PATH] = {0};
	if (GetWindowsDirectoryW(windir, MAX_PATH) == 0 ||
	    ! GetVolumePathNameW(windir, root, MAX_PATH) ||
	    ! GetVolumeNameForVolumeMountPointW(root, guid, MAX_PATH))
		return std::wstring();
	return guid;
}


// NT device name of the firmware system partition (ESP or "System Reserved").
static std::wstring boot_partition_device()
{
	HKEY key;
	if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\Setup", 0, KEY_READ, &key) != ERROR_SUCCESS)
		return std::wstring();
	wchar_t value[MAX_PATH] = {0};
	DWORD size = sizeof(value) - sizeof(wchar_t);
	DWORD type = 0;
	std::wstring result;
	if (RegQueryValueExW(key, L"SystemPartition", nullptr, &type, reinterpret_cast<LPBYTE>(value), &size) == ERROR_SUCCESS &&
	    type == REG_SZ)
		result = value;
	RegCloseKey(key);
	return result;
}


static void read_extents(Volume& v)
{
	HANDLE h = CreateFileW(device_path(v).c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
	                       OPEN_EXISTING, 0, nullptr);
	if (h == INVALID_HANDLE_VALUE)
		return;
	std::vector<BYTE> buf(sizeof(VOLUME_DISK_EXTENTS) + 8 * sizeof(DISK_EXTENT));
	DWORD returned = 0;
	BOOL ok = DeviceIoControl(h, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, nullptr, 0, buf.data(),
	                          static_cast<DWORD>(buf.size()), &returned, nullptr);
	if (! ok && GetLastError() == ERROR_MORE_DATA)
	{
		DWORD count = reinterpret_cast<VOLUME_DISK_EXTENTS*>(buf.data())->NumberOfDiskExtents;
		buf.resize(sizeof(VOLUME_DISK_EXTENTS) + count * sizeof(DISK_EXTENT));
		ok = DeviceIoControl(h, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, nullptr, 0, buf.data(),
		                     static_cast<DWORD>(buf.size()), &returned, nullptr);
	}
	CloseHandle(h);
	if (! ok)
		return;
	const VOLUME_DISK_EXTENTS* ext = reinterpret_cast<const VOLUME_DISK_EXTENTS*>(buf.data());
	for (DWORD i = 0; i < ext->NumberOfDiskExtents; i++)
	{
		Extent e;
		e.disk   = ext->Extents[i].DiskNumber;
		e.offset = static_cast<uint64_t>(ext->Extents[i].StartingOffset.QuadPart);
		e.length = static_cast<uint64_t>(ext->Extents[i].ExtentLength.QuadPart);
		v.extents.push_back(e);
	}
}


static void read_mount_points(Volume& v)
{
	DWORD needed = 0;
	std::vector<wchar_t> buf(MAX_PATH);
	if (! GetVolumePathNamesForVolumeNameW(v.guid_path.c_str(), buf.data(), static_cast<DWORD>(buf.size()), &needed))
	{
		if (GetLastError() != ERROR_MORE_DATA)
			return;
		buf.resize(needed);
		if (! GetVolumePathNamesForVolumeNameW(v.guid_path.c_str(), buf.data(), static_cast<DWORD>(buf.size()), &needed))
			return;
	}
	for (const wchar_t* p = buf.data(); *p; p += wcslen(p) + 1)
		v.mount_points.push_back(p);
}


static bool file_exists(const std::wstring& path)
{
	return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}


std::vector<Volume> enumerate()
{
	std::vector<Volume> out;
	const std::wstring sys_guid = system_volume_guid();
	const std::wstring boot_dev = boot_partition_device();

	wchar_t name[MAX_PATH] = {0};
	HANDLE find = FindFirstVolumeW(name, MAX_PATH);
	if (find == INVALID_HANDLE_VALUE)
		return out;
	do
	{
		Volume v;
		v.guid_path = name;
		read_extents(v);
		read_mount_points(v);

		wchar_t label[MAX_PATH + 1] = {0};
		wchar_t fs[MAX_PATH + 1] = {0};
		DWORD serial = 0, max_component = 0, flags = 0;
		if (GetVolumeInformationW(v.guid_path.c_str(), label, MAX_PATH + 1, &serial, &max_component,
		                          &flags, fs, MAX_PATH + 1))
		{
			v.fs_mounted   = true;
			v.fs_name      = fs;
			v.label        = label;
			v.serial       = serial;
			v.fs_read_only = (flags & FILE_READ_ONLY_VOLUME) != 0;
			v.paging = file_exists(v.guid_path + L"pagefile.sys") ||
			           file_exists(v.guid_path + L"swapfile.sys") ||
			           file_exists(v.guid_path + L"hiberfil.sys");
		}
		v.system = (! sys_guid.empty() && _wcsicmp(sys_guid.c_str(), v.guid_path.c_str()) == 0);
		if (! v.system && ! boot_dev.empty())
		{
			std::wstring nt = volume_nt_device(v.guid_path);
			v.system = ! nt.empty() && _wcsicmp(nt.c_str(), boot_dev.c_str()) == 0;
		}
		out.push_back(v);
	}
	while (FindNextVolumeW(find, name, MAX_PATH));
	FindVolumeClose(find);
	return out;
}


bool find_partition_volume(uint32_t disk, uint64_t offset, uint64_t length, Volume& out)
{
	for (const Volume& v : enumerate())
	{
		if (v.exactly(disk, offset, length))
		{
			out = v;
			return true;
		}
	}
	return false;
}


bool wait_for_partition_volume(uint32_t disk, uint64_t offset, uint64_t length,
                               unsigned timeout_ms, Volume& out)
{
	auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
	for (;;)
	{
		if (find_partition_volume(disk, offset, length, out))
			return true;
		if (std::chrono::steady_clock::now() >= deadline)
			return false;
		std::this_thread::sleep_for(std::chrono::milliseconds(250));
	}
}


Lock::~Lock()
{
	release();
}


bool Lock::lock(const Volume& volume, std::string& error)
{
	release();
	if (volume.busy())
	{
		error = "Volume " + to_utf8(volume.guid_path) + " is in use: " + volume.busy_reason();
		return false;
	}
	m_path = device_path(volume);
	HANDLE h = CreateFileW(m_path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
	                       nullptr, OPEN_EXISTING, 0, nullptr);
	if (h == INVALID_HANDLE_VALUE)
	{
		error = "Unable to open volume " + to_utf8(volume.guid_path) + ": " + last_error_message(GetLastError());
		return false;
	}
	m_handle = h;

	DWORD returned = 0;
	DWORD last = 0;
	for (int attempt = 0; attempt < 10; attempt++)
	{
		if (DeviceIoControl(h, FSCTL_LOCK_VOLUME, nullptr, 0, nullptr, 0, &returned, nullptr))
		{
			m_locked = true;
			break;
		}
		last = GetLastError();
		std::this_thread::sleep_for(std::chrono::milliseconds(200));
	}
	if (! m_locked)
	{
		error = "Volume " + to_utf8(volume.guid_path) + " is in use by another program and can not "
		        "be locked (close any program or Explorer window using it): " + last_error_message(last);
		release();
		return false;
	}
	if (! DeviceIoControl(h, FSCTL_DISMOUNT_VOLUME, nullptr, 0, nullptr, 0, &returned, nullptr))
	{
		error = "Unable to dismount volume " + to_utf8(volume.guid_path) + ": " +
		        last_error_message(GetLastError());
		release();
		return false;
	}
	return true;
}


void Lock::release()
{
	HANDLE h = static_cast<HANDLE>(m_handle);
	if (h && h != INVALID_HANDLE_VALUE)
	{
		if (m_locked)
		{
			DWORD returned = 0;
			DeviceIoControl(h, FSCTL_UNLOCK_VOLUME, nullptr, 0, nullptr, 0, &returned, nullptr);
		}
		CloseHandle(h);
	}
	m_handle = nullptr;
	m_locked = false;
}


bool lock_ranges(uint32_t disk, const std::vector<Extent>& ranges,
                 std::vector<std::unique_ptr<Lock>>& locks, std::string& error)
{
	locks.clear();
	for (const Volume& v : enumerate())
	{
		bool affected = false;
		for (const Extent& r : ranges)
			affected = affected || v.on_disk_range(disk, r.offset, r.length);
		if (! affected)
			continue;
		std::unique_ptr<Lock> l(new Lock());
		if (! l->lock(v, error))
		{
			locks.clear();
			return false;
		}
		locks.push_back(std::move(l));
	}
	return true;
}


}  // namespace Win32Volumes
}  // namespace WParted

#endif /* _WIN32 */
