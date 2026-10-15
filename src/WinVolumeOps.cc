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

#include "WinVolumeOps.h"
#include "Win32Volumes.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winioctl.h>
#include <objbase.h>
#include <wbemidl.h>
#include <oleauto.h>

#include <cstdio>
#include <cstring>
#include <vector>


namespace WParted
{
namespace WinVolumeOps
{

namespace
{

using Win32Volumes::Volume;
using Win32Volumes::to_utf8;
using Win32Volumes::from_utf8;
using Win32Volumes::last_error_message;


bool find_volume(const Target& t, Volume& v, std::string& error, unsigned wait_ms = 0)
{
	if (t.disk < 0 || t.length == 0)
	{
		error = "Invalid partition location";
		return false;
	}
	bool found = wait_ms ? Win32Volumes::wait_for_partition_volume(t.disk, t.offset, t.length, wait_ms, v)
	                     : Win32Volumes::find_partition_volume(t.disk, t.offset, t.length, v);
	if (! found)
	{
		error = "Windows has no volume for the partition at offset " + std::to_string(t.offset) +
		        " on disk " + std::to_string(t.disk);
		return false;
	}
	return true;
}


std::wstring wql_quote(const std::wstring& s)
{
	std::wstring out;
	for (wchar_t c : s)
	{
		if (c == L'\\' || c == L'\'')
			out.push_back(L'\\');
		out.push_back(c);
	}
	return out;
}


template <typename T>
class ComPtr
{
public:
	ComPtr() = default;
	~ComPtr() { reset(); }
	ComPtr(const ComPtr&) = delete;
	ComPtr& operator=(const ComPtr&) = delete;
	T** out() { reset(); return &m_p; }
	T* get() const { return m_p; }
	T* operator->() const { return m_p; }
	void reset() { if (m_p) m_p->Release(); m_p = nullptr; }
private:
	T* m_p = nullptr;
};


class Bstr
{
public:
	explicit Bstr(const std::wstring& s) : m_b(SysAllocString(s.c_str())) {}
	~Bstr() { if (m_b) SysFreeString(m_b); }
	Bstr(const Bstr&) = delete;
	Bstr& operator=(const Bstr&) = delete;
	BSTR get() const { return m_b; }
private:
	BSTR m_b;
};


std::string hr_message(const std::string& what, HRESULT hr)
{
	char buf[32];
	std::snprintf(buf, sizeof(buf), "0x%08lX", static_cast<unsigned long>(hr));
	return what + " failed (" + buf + ")";
}


// Connection to the Windows Storage Management provider, valid in the calling
// thread for the lifetime of the object.
class StorageWmi
{
public:
	StorageWmi()
	{
		HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		m_uninit = SUCCEEDED(hr);
		if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
		{
			m_error = hr_message("CoInitializeEx", hr);
			return;
		}
		// RPC_E_TOO_LATE just means security was already initialised.
		CoInitializeSecurity(nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_DEFAULT,
		                     RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr);

		ComPtr<IWbemLocator> locator;
		hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_IWbemLocator,
		                      reinterpret_cast<void**>(locator.out()));
		if (FAILED(hr))
		{
			m_error = hr_message("Creating the WMI locator", hr);
			return;
		}
		Bstr ns(L"ROOT\\Microsoft\\Windows\\Storage");
		hr = locator->ConnectServer(ns.get(), nullptr, nullptr, nullptr, 0, nullptr, nullptr, m_svc.out());
		if (FAILED(hr))
		{
			m_error = hr_message("Connecting to the Windows Storage Management provider", hr);
			return;
		}
		hr = CoSetProxyBlanket(m_svc.get(), RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
		                       RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
		if (FAILED(hr))
		{
			m_error = hr_message("CoSetProxyBlanket", hr);
			m_svc.reset();
		}
	}

	~StorageWmi()
	{
		m_svc.reset();
		if (m_uninit)
			CoUninitialize();
	}

	StorageWmi(const StorageWmi&) = delete;
	StorageWmi& operator=(const StorageWmi&) = delete;

	bool ok(std::string& error) const
	{
		if (m_svc.get())
			return true;
		error = m_error;
		return false;
	}

	// __PATH of the single object returned by a query, optionally filtered.
	bool query_path(const std::wstring& wql, std::wstring& path, std::string& error,
	                std::function<bool(IWbemClassObject*)> check = nullptr)
	{
		Bstr lang(L"WQL");
		Bstr query(wql);
		ComPtr<IEnumWbemClassObject> e;
		HRESULT hr = m_svc->ExecQuery(lang.get(), query.get(),
		                              WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, e.out());
		if (FAILED(hr))
		{
			error = hr_message("WMI query", hr);
			return false;
		}
		CoSetProxyBlanket(e.get(), RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL,
		                  RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
		int found = 0;
		for (;;)
		{
			ComPtr<IWbemClassObject> obj;
			ULONG n = 0;
			hr = e->Next(WBEM_INFINITE, 1, obj.out(), &n);
			if (hr != WBEM_S_NO_ERROR || n == 0)
				break;
			if (check && ! check(obj.get()))
				continue;
			VARIANT v;
			VariantInit(&v);
			if (SUCCEEDED(obj->Get(L"__PATH", 0, &v, nullptr, nullptr)) && v.vt == VT_BSTR)
			{
				path = v.bstrVal;
				found++;
			}
			VariantClear(&v);
		}
		if (found != 1)
		{
			error = found == 0 ? "Windows Storage Management did not find the partition or volume"
			                   : "Windows Storage Management returned more than one match";
			return false;
		}
		return true;
	}

	// Execute a method.  Returns false on COM failure or a non zero ReturnValue.
	bool exec(const std::wstring& cls, const std::wstring& path, const std::wstring& method,
	          std::function<void(IWbemClassObject*)> fill, ComPtr<IWbemClassObject>& out,
	          std::string& error)
	{
		Bstr bcls(cls);
		ComPtr<IWbemClassObject> class_obj;
		HRESULT hr = m_svc->GetObject(bcls.get(), 0, nullptr, class_obj.out(), nullptr);
		if (FAILED(hr))
		{
			error = hr_message("Getting WMI class " + to_utf8(cls), hr);
			return false;
		}
		ComPtr<IWbemClassObject> in_def;
		ComPtr<IWbemClassObject> in;
		hr = class_obj->GetMethod(method.c_str(), 0, in_def.out(), nullptr);
		if (FAILED(hr))
		{
			error = hr_message("Getting WMI method " + to_utf8(method), hr);
			return false;
		}
		if (in_def.get())
		{
			hr = in_def->SpawnInstance(0, in.out());
			if (FAILED(hr))
			{
				error = hr_message("Creating WMI method parameters", hr);
				return false;
			}
			if (fill)
				fill(in.get());
		}
		Bstr bpath(path);
		Bstr bmethod(method);
		hr = m_svc->ExecMethod(bpath.get(), bmethod.get(), 0, nullptr, in.get(), out.out(), nullptr);
		if (FAILED(hr))
		{
			error = hr_message("Calling " + to_utf8(cls + L"." + method), hr);
			return false;
		}
		unsigned long rv = 0;
		VARIANT v;
		VariantInit(&v);
		if (SUCCEEDED(out->Get(L"ReturnValue", 0, &v, nullptr, nullptr)))
		{
			if (v.vt == VT_I4)
				rv = static_cast<unsigned long>(v.lVal);
			else if (v.vt == VT_UI4)
				rv = v.ulVal;
		}
		VariantClear(&v);
		if (rv != 0)
		{
			error = to_utf8(cls + L"." + method) + ": " + return_value_text(rv) + extended_status(out.get());
			return false;
		}
		return true;
	}

	static void put_string(IWbemClassObject* in, const wchar_t* name, const std::wstring& value)
	{
		VARIANT v;
		VariantInit(&v);
		v.vt = VT_BSTR;
		v.bstrVal = SysAllocString(value.c_str());
		in->Put(name, 0, &v, 0);
		VariantClear(&v);
	}

	static void put_bool(IWbemClassObject* in, const wchar_t* name, bool value)
	{
		VARIANT v;
		VariantInit(&v);
		v.vt = VT_BOOL;
		v.boolVal = value ? VARIANT_TRUE : VARIANT_FALSE;
		in->Put(name, 0, &v, 0);
	}

	// CIM uint64 values are passed as decimal strings.
	static void put_uint64(IWbemClassObject* in, const wchar_t* name, uint64_t value)
	{
		put_string(in, name, std::to_wstring(value));
	}

	static bool get_uint64(IWbemClassObject* obj, const wchar_t* name, uint64_t& value)
	{
		VARIANT v;
		VariantInit(&v);
		bool ok = false;
		if (SUCCEEDED(obj->Get(name, 0, &v, nullptr, nullptr)))
		{
			if (v.vt == VT_BSTR)
			{
				value = _wcstoui64(v.bstrVal, nullptr, 10);
				ok = true;
			}
			else if (v.vt == VT_UI8 || v.vt == VT_I8)
			{
				value = static_cast<uint64_t>(v.ullVal);
				ok = true;
			}
		}
		VariantClear(&v);
		return ok;
	}

private:
	static std::string return_value_text(unsigned long rv)
	{
		switch (rv)
		{
			case 1:     return "not supported";
			case 2:     return "unspecified error";
			case 3:     return "timed out";
			case 4:     return "failed";
			case 5:     return "invalid parameter";
			case 6:     return "access denied";
			case 40000: return "not supported";
			case 40001: return "access denied";
			case 40002: return "there are not enough resources to complete the operation";
			default:    return "error " + std::to_string(rv);
		}
	}

	// MSFT_StorageExtendedStatus comes back as embedded object text.
	static std::string extended_status(IWbemClassObject* out)
	{
		VARIANT v;
		VariantInit(&v);
		std::string text;
		if (SUCCEEDED(out->Get(L"ExtendedStatus", 0, &v, nullptr, nullptr)) && v.vt == VT_BSTR && v.bstrVal)
		{
			std::wstring s = v.bstrVal;
			size_t pos = s.find(L"Message = \"");
			if (pos != std::wstring::npos)
			{
				pos += 11;
				size_t end = s.find(L"\";", pos);
				text = " (" + to_utf8(s.substr(pos, end == std::wstring::npos ? std::wstring::npos : end - pos)) + ")";
			}
		}
		VariantClear(&v);
		return text;
	}

	ComPtr<IWbemServices> m_svc;
	bool                  m_uninit = false;
	std::string           m_error;
};


bool partition_wmi_path(StorageWmi& wmi, const Target& t, std::wstring& path, std::string& error)
{
	std::wstring wql = L"SELECT * FROM MSFT_Partition WHERE DiskNumber = " + std::to_wstring(t.disk) +
	                   L" AND Offset = " + std::to_wstring(t.offset);
	return wmi.query_path(wql, path, error, [&t](IWbemClassObject* obj)
	{
		uint64_t size = 0;
		return StorageWmi::get_uint64(obj, L"Size", size) && size == t.length;
	});
}

}  // anonymous namespace


bool format(const Target& t, const std::string& windows_fs, const std::string& label, std::string& error)
{
	Volume v;
	// The volume of a partition created moments ago may still be arriving.
	if (! find_volume(t, v, error, 30000))
		return false;
	if (v.system || v.paging)
	{
		error = "Refusing to format: " + v.busy_reason();
		return false;
	}

	StorageWmi wmi;
	std::wstring path;
	if (! wmi.ok(error) ||
	    ! wmi.query_path(L"SELECT * FROM MSFT_Volume WHERE Path = '" + wql_quote(v.guid_path) + L"'", path, error))
		return false;

	std::wstring fs = from_utf8(windows_fs);
	std::wstring wlabel = from_utf8(label);
	ComPtr<IWbemClassObject> out;
	return wmi.exec(L"MSFT_Volume", path, L"Format", [&](IWbemClassObject* in)
	{
		StorageWmi::put_string(in, L"FileSystem", fs);
		StorageWmi::put_string(in, L"FileSystemLabel", wlabel);
		StorageWmi::put_bool(in, L"Full", false);
		StorageWmi::put_bool(in, L"Force", true);
	}, out, error);
}


bool set_label(const Target& t, const std::string& label, std::string& error)
{
	Volume v;
	if (! find_volume(t, v, error, 10000))
		return false;
	std::wstring wlabel = from_utf8(label);
	if (! SetVolumeLabelW(v.guid_path.c_str(), wlabel.empty() ? nullptr : wlabel.c_str()))
	{
		error = "SetVolumeLabel: " + last_error_message(GetLastError());
		return false;
	}
	return true;
}


bool check_repair(const Target& t, std::function<void(const std::string&)> output,
                  std::string& command_line, int& exit_code, std::string& error)
{
	exit_code = -1;
	Volume v;
	if (! find_volume(t, v, error, 10000))
		return false;
	if (v.busy())
	{
		error = "Refusing to check: " + v.busy_reason();
		return false;
	}

	wchar_t sysdir[MAX_PATH] = {0};
	if (GetSystemDirectoryW(sysdir, MAX_PATH) == 0)
	{
		error = "GetSystemDirectory: " + last_error_message(GetLastError());
		return false;
	}
	std::wstring exe = std::wstring(sysdir) + L"\\chkdsk.exe";
	std::wstring cmdline = L"\"" + exe + L"\" " + Win32Volumes::device_path(v) + L" /f /x";
	command_line = to_utf8(cmdline);

	SECURITY_ATTRIBUTES sa;
	sa.nLength = sizeof(sa);
	sa.lpSecurityDescriptor = nullptr;
	sa.bInheritHandle = TRUE;
	HANDLE read_pipe = nullptr, write_pipe = nullptr;
	if (! CreatePipe(&read_pipe, &write_pipe, &sa, 0))
	{
		error = "CreatePipe: " + last_error_message(GetLastError());
		return false;
	}
	SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);
	// Any question chkdsk might ask is answered by end of file (no).
	HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
	                         OPEN_EXISTING, 0, nullptr);

	STARTUPINFOW si;
	std::memset(&si, 0, sizeof(si));
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdInput = nul;
	si.hStdOutput = write_pipe;
	si.hStdError = write_pipe;
	PROCESS_INFORMATION pi;
	std::memset(&pi, 0, sizeof(pi));
	std::vector<wchar_t> cmd(cmdline.begin(), cmdline.end());
	cmd.push_back(L'\0');
	BOOL started = CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
	                              nullptr, sysdir, &si, &pi);
	DWORD start_error = GetLastError();
	CloseHandle(write_pipe);
	if (nul != INVALID_HANDLE_VALUE)
		CloseHandle(nul);
	if (! started)
	{
		CloseHandle(read_pipe);
		error = "CreateProcess: " + last_error_message(start_error);
		return false;
	}

	// chkdsk writes in the OEM code page when its output is redirected.
	std::string raw;
	char buf[4096];
	DWORD n = 0;
	while (ReadFile(read_pipe, buf, sizeof(buf), &n, nullptr) && n > 0)
	{
		raw.append(buf, n);
		if (! output)
			continue;
		int wn = MultiByteToWideChar(CP_OEMCP, 0, raw.data(), static_cast<int>(raw.size()), nullptr, 0);
		std::wstring w(wn > 0 ? wn : 0, L'\0');
		if (wn > 0)
			MultiByteToWideChar(CP_OEMCP, 0, raw.data(), static_cast<int>(raw.size()), &w[0], wn);
		// Keep only the final state of lines rewritten with carriage returns.
		std::wstring text, line;
		for (wchar_t c : w)
		{
			if (c == L'\r')
				line.clear();
			else if (c == L'\n')
			{
				text += line + L"\n";
				line.clear();
			}
			else
				line.push_back(c);
		}
		output(to_utf8(text + line));
	}
	CloseHandle(read_pipe);
	WaitForSingleObject(pi.hProcess, INFINITE);
	DWORD code = 255;
	GetExitCodeProcess(pi.hProcess, &code);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	exit_code = static_cast<int>(code);

	// chkdsk: 0 no errors, 1 errors found and fixed, 2 disk cleanup performed,
	// 3 could not check or errors could not be fixed.
	if (code > 2)
	{
		error = "chkdsk reported that the volume could not be checked or repaired (exit code " +
		        std::to_string(code) + ")";
		return false;
	}
	return true;
}


bool supported_size(const Target& t, uint64_t& min_bytes, uint64_t& max_bytes, std::string& error)
{
	StorageWmi wmi;
	std::wstring path;
	if (! wmi.ok(error) || ! partition_wmi_path(wmi, t, path, error))
		return false;
	ComPtr<IWbemClassObject> out;
	if (! wmi.exec(L"MSFT_Partition", path, L"GetSupportedSize", nullptr, out, error))
		return false;
	if (! StorageWmi::get_uint64(out.get(), L"SizeMin", min_bytes) ||
	    ! StorageWmi::get_uint64(out.get(), L"SizeMax", max_bytes))
	{
		error = "Windows did not report the supported partition sizes";
		return false;
	}
	return true;
}


bool resize(const Target& t, uint64_t new_length, std::string& error)
{
	uint64_t min_bytes = 0, max_bytes = 0;
	if (! supported_size(t, min_bytes, max_bytes, error))
		return false;
	if (new_length < min_bytes || new_length > max_bytes)
	{
		error = "The requested size " + std::to_string(new_length) + " bytes is outside the range Windows "
		        "supports for this partition (" + std::to_string(min_bytes) + " to " +
		        std::to_string(max_bytes) + " bytes).  Nothing was changed.";
		return false;
	}
	StorageWmi wmi;
	std::wstring path;
	if (! wmi.ok(error) || ! partition_wmi_path(wmi, t, path, error))
		return false;
	ComPtr<IWbemClassObject> out;
	return wmi.exec(L"MSFT_Partition", path, L"Resize", [&](IWbemClassObject* in)
	{
		StorageWmi::put_uint64(in, L"Size", new_length);
	}, out, error);
}


bool grow_filesystem_to_partition(const Target& t, bool& already, std::string& error)
{
	already = false;
	Volume v;
	if (! find_volume(t, v, error, 10000))
		return false;
	HANDLE h = CreateFileW(Win32Volumes::device_path(v).c_str(), GENERIC_READ | GENERIC_WRITE,
	                       FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
	if (h == INVALID_HANDLE_VALUE)
	{
		error = "Unable to open the volume: " + last_error_message(GetLastError());
		return false;
	}
	NTFS_VOLUME_DATA_BUFFER data;
	DWORD returned = 0;
	if (! DeviceIoControl(h, FSCTL_GET_NTFS_VOLUME_DATA, nullptr, 0, &data, sizeof(data), &returned, nullptr))
	{
		error = "FSCTL_GET_NTFS_VOLUME_DATA: " + last_error_message(GetLastError());
		CloseHandle(h);
		return false;
	}
	uint64_t fs_bytes = uint64_t(data.NumberSectors.QuadPart) * data.BytesPerSector;
	// NTFS keeps a backup boot sector in the last sector of the partition.
	if (fs_bytes + 2ULL * 1024 * 1024 >= t.length)
	{
		already = true;
		CloseHandle(h);
		return true;
	}
	LONGLONG new_sectors = static_cast<LONGLONG>(t.length / data.BytesPerSector) - 1;
	bool ok = DeviceIoControl(h, FSCTL_EXTEND_VOLUME, &new_sectors, sizeof(new_sectors), nullptr, 0,
	                          &returned, nullptr) != FALSE;
	if (! ok)
		error = "FSCTL_EXTEND_VOLUME: " + last_error_message(GetLastError());
	CloseHandle(h);
	return ok;
}


bool usage(const Target& t, int64_t& fs_size, int64_t& fs_free, std::string& error)
{
	Volume v;
	if (! find_volume(t, v, error))
		return false;
	if (! v.fs_mounted)
	{
		error = "Windows has not mounted the file system";
		return false;
	}
	ULARGE_INTEGER avail, total, free;
	if (! GetDiskFreeSpaceExW(v.guid_path.c_str(), &avail, &total, &free))
	{
		error = "GetDiskFreeSpaceEx: " + last_error_message(GetLastError());
		return false;
	}
	fs_size = static_cast<int64_t>(total.QuadPart);
	fs_free = static_cast<int64_t>(free.QuadPart);
	return true;
}


bool mount(const Target& t, std::string& mount_point, std::string& error)
{
	Volume v;
	if (! find_volume(t, v, error))
		return false;
	DWORD used = GetLogicalDrives();
	for (wchar_t letter = L'D'; letter <= L'Z'; letter++)
	{
		if (used & (1u << (letter - L'A')))
			continue;
		std::wstring root = std::wstring(1, letter) + L":\\";
		if (SetVolumeMountPointW(root.c_str(), v.guid_path.c_str()))
		{
			mount_point = to_utf8(root);
			return true;
		}
		error = "SetVolumeMountPoint: " + last_error_message(GetLastError());
		return false;
	}
	error = "No free drive letter";
	return false;
}


bool unmount(const Target& t, std::string& error)
{
	Volume v;
	if (! find_volume(t, v, error))
		return false;
	if (v.system || v.paging)
	{
		error = "Windows is using this volume: " + v.busy_reason();
		return false;
	}
	for (const std::wstring& mp : v.mount_points)
	{
		if (! DeleteVolumeMountPointW(mp.c_str()))
		{
			error = "DeleteVolumeMountPoint(" + to_utf8(mp) + "): " + last_error_message(GetLastError());
			return false;
		}
	}
	return true;
}


}  // namespace WinVolumeOps
}  // namespace WParted

#endif /* _WIN32 */
