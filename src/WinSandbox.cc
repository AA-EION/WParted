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

#include "WinSandbox.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <sddl.h>
#include <mutex>
#include <thread>
#endif


namespace WParted
{
namespace WinSandbox
{


std::wstring quote_argument(const std::wstring& arg)
{
	if (! arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos)
		return arg;
	std::wstring out = L"\"";
	size_t backslashes = 0;
	for (wchar_t c : arg)
	{
		if (c == L'\\')
		{
			backslashes++;
			continue;
		}
		if (c == L'"')
		{
			out.append(backslashes * 2 + 1, L'\\');
			out.push_back(L'"');
		}
		else
		{
			out.append(backslashes, L'\\');
			out.push_back(c);
		}
		backslashes = 0;
	}
	out.append(backslashes * 2, L'\\');
	out.push_back(L'"');
	return out;
}


static bool valid_helper_name(const std::string& name)
{
	if (name.empty() || name.size() > 64 || name[0] == '.')
		return false;
	for (char c : name)
		if (! ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
		       c == '.' || c == '_' || c == '-'))
			return false;
	return name.find("..") == std::string::npos;
}


#if defined(_WIN32)

namespace
{

std::wstring widen(const std::string& s)
{
	if (s.empty())
		return std::wstring();
	int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
	if (n <= 0)
		return std::wstring();
	std::wstring w(static_cast<size_t>(n), L'\0');
	MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), &w[0], n);
	return w;
}


std::string narrow(const std::wstring& w)
{
	if (w.empty())
		return std::string();
	int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
	std::string s(static_cast<size_t>(n), '\0');
	WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
	return s;
}


std::string win_error(DWORD code)
{
	wchar_t* buf = nullptr;
	FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
	               nullptr, code, 0, reinterpret_cast<wchar_t*>(&buf), 0, nullptr);
	std::string msg = buf ? narrow(buf) : std::string();
	if (buf)
		LocalFree(buf);
	while (! msg.empty() && (msg.back() == '\n' || msg.back() == '\r' || msg.back() == ' ' || msg.back() == '.'))
		msg.pop_back();
	return msg + " (" + std::to_string(code) + ")";
}


class Handle
{
public:
	Handle() = default;
	explicit Handle(HANDLE h) : m_h(h) {}
	~Handle()  { reset(); }
	Handle(const Handle&) = delete;
	Handle& operator=(const Handle&) = delete;
	HANDLE get() const  { return m_h; }
	HANDLE* out()  { reset(); return &m_h; }
	bool valid() const  { return m_h && m_h != INVALID_HANDLE_VALUE; }
	void reset()
	{
		if (valid())
			CloseHandle(m_h);
		m_h = nullptr;
	}
private:
	HANDLE m_h = nullptr;
};


std::wstring executable_dir()
{
	std::wstring buf(32768, L'\0');
	DWORD n = GetModuleFileNameW(nullptr, &buf[0], static_cast<DWORD>(buf.size()));
	if (n == 0 || n >= buf.size())
		return std::wstring();
	buf.resize(n);
	size_t slash = buf.find_last_of(L"\\/");
	return slash == std::wstring::npos ? std::wstring() : buf.substr(0, slash);
}


// Restricted, low integrity copy of our own (elevated) token.
bool make_token(Handle& token, std::string& error)
{
	Handle self;
	if (! OpenProcessToken(GetCurrentProcess(),
	                       TOKEN_DUPLICATE | TOKEN_QUERY | TOKEN_ASSIGN_PRIMARY | TOKEN_ADJUST_DEFAULT,
	                       self.out()))
	{
		error = "OpenProcessToken: " + win_error(GetLastError());
		return false;
	}

	BYTE admins[SECURITY_MAX_SID_SIZE];
	BYTE local_admin[SECURITY_MAX_SID_SIZE];
	DWORD size = sizeof(admins);
	SID_AND_ATTRIBUTES disable[2];
	DWORD disable_count = 0;
	if (! CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, admins, &size))
	{
		error = "CreateWellKnownSid: " + win_error(GetLastError());
		return false;
	}
	disable[disable_count].Sid = admins;
	disable[disable_count].Attributes = 0;
	disable_count++;
	size = sizeof(local_admin);
	if (CreateWellKnownSid(WinLocalAccountAndAdministratorSid, nullptr, local_admin, &size))
	{
		disable[disable_count].Sid = local_admin;
		disable[disable_count].Attributes = 0;
		disable_count++;
	}

	if (! CreateRestrictedToken(self.get(), DISABLE_MAX_PRIVILEGE, disable_count, disable, 0, nullptr,
	                            0, nullptr, token.out()))
	{
		error = "CreateRestrictedToken: " + win_error(GetLastError());
		return false;
	}

	PSID low = nullptr;
	if (! ConvertStringSidToSidW(L"S-1-16-4096", &low))
	{
		error = "ConvertStringSidToSid: " + win_error(GetLastError());
		return false;
	}
	TOKEN_MANDATORY_LABEL label = {};
	label.Label.Attributes = SE_GROUP_INTEGRITY;
	label.Label.Sid = low;
	BOOL ok = SetTokenInformation(token.get(), TokenIntegrityLevel, &label,
	                              sizeof(label) + GetLengthSid(low));
	DWORD err = GetLastError();
	LocalFree(low);
	if (! ok)
	{
		error = "SetTokenInformation(integrity): " + win_error(err);
		return false;
	}
	return true;
}


bool make_job(Handle& job, uint64_t memory_limit, std::string& error)
{
	job.out();
	*job.out() = CreateJobObjectW(nullptr, nullptr);
	if (! job.valid())
	{
		error = "CreateJobObject: " + win_error(GetLastError());
		return false;
	}
	JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
	limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE |
	                                          JOB_OBJECT_LIMIT_ACTIVE_PROCESS |
	                                          JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION |
	                                          JOB_OBJECT_LIMIT_PROCESS_MEMORY;
	limits.BasicLimitInformation.ActiveProcessLimit = 1;
	limits.ProcessMemoryLimit = static_cast<SIZE_T>(memory_limit);
	if (! SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
	{
		error = "SetInformationJobObject(limits): " + win_error(GetLastError());
		return false;
	}
	JOBOBJECT_BASIC_UI_RESTRICTIONS ui = {};
	ui.UIRestrictionsClass = JOB_OBJECT_UILIMIT_DESKTOP | JOB_OBJECT_UILIMIT_DISPLAYSETTINGS |
	                         JOB_OBJECT_UILIMIT_EXITWINDOWS | JOB_OBJECT_UILIMIT_GLOBALATOMS |
	                         JOB_OBJECT_UILIMIT_HANDLES | JOB_OBJECT_UILIMIT_READCLIPBOARD |
	                         JOB_OBJECT_UILIMIT_SYSTEMPARAMETERS | JOB_OBJECT_UILIMIT_WRITECLIPBOARD;
	if (! SetInformationJobObject(job.get(), JobObjectBasicUIRestrictions, &ui, sizeof(ui)))
	{
		error = "SetInformationJobObject(ui): " + win_error(GetLastError());
		return false;
	}
	return true;
}


// Pipe with one inheritable end for the child.
bool make_pipe(Handle& parent_end, Handle& child_end, bool child_reads, std::string& error)
{
	SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
	HANDLE rd = nullptr, wr = nullptr;
	if (! CreatePipe(&rd, &wr, &sa, 1024 * 1024))
	{
		error = "CreatePipe: " + win_error(GetLastError());
		return false;
	}
	*parent_end.out() = child_reads ? wr : rd;
	*child_end.out()  = child_reads ? rd : wr;
	SetHandleInformation(parent_end.get(), HANDLE_FLAG_INHERIT, 0);
	return true;
}


class HandleChannel : public Broker::Channel
{
public:
	HandleChannel(HANDLE rd, HANDLE wr) : m_rd(rd), m_wr(wr) {}
	bool read_exact(void* buffer, size_t length) override
	{
		uint8_t* p = static_cast<uint8_t*>(buffer);
		while (length)
		{
			DWORD n = 0;
			DWORD want = length > 0x10000000 ? 0x10000000 : static_cast<DWORD>(length);
			if (! ReadFile(m_rd, p, want, &n, nullptr) || n == 0)
				return false;
			p += n;
			length -= n;
		}
		return true;
	}
	bool write_all(const void* buffer, size_t length) override
	{
		const uint8_t* p = static_cast<const uint8_t*>(buffer);
		while (length)
		{
			DWORD n = 0;
			DWORD want = length > 0x10000000 ? 0x10000000 : static_cast<DWORD>(length);
			if (! WriteFile(m_wr, p, want, &n, nullptr) || n == 0)
				return false;
			p += n;
			length -= n;
		}
		return true;
	}
private:
	HANDLE m_rd;
	HANDLE m_wr;
};


void read_stream(HANDLE h, std::string& sink, bool is_stderr,
                 const std::function<void(const std::string&, bool)>& on_output, std::mutex& mutex)
{
	char buf[4096];
	for (;;)
	{
		DWORD n = 0;
		if (! ReadFile(h, buf, sizeof(buf), &n, nullptr) || n == 0)
			return;
		std::string chunk(buf, n);
		std::lock_guard<std::mutex> lock(mutex);
		// Cap captured output; the helper can still be followed through on_output.
		if (sink.size() < 16 * 1024 * 1024)
			sink += chunk;
		if (on_output)
			on_output(chunk, is_stderr);
	}
}

}  // anonymous namespace


std::string helper_path(const std::string& name)
{
	if (! valid_helper_name(name))
		return std::string();
	std::wstring dir = executable_dir();
	if (dir.empty())
		return std::string();
	// <prefix>\bin\wparted.exe -> <prefix>\lib\wparted\<name>.exe
	size_t slash = dir.find_last_of(L"\\/");
	std::wstring prefix = slash == std::wstring::npos ? dir : dir.substr(0, slash);
	std::wstring path = prefix + L"\\lib\\wparted\\" + widen(name) + L".exe";
	DWORD attrs = GetFileAttributesW(path.c_str());
	if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
	{
		// Tests run the helpers from the build tree, next to the test program.
		path = dir + L"\\" + widen(name) + L".exe";
		attrs = GetFileAttributesW(path.c_str());
		if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
			return std::string();
	}
	return narrow(path);
}


Result run(const Options& options)
{
	Result result;
	std::string path = helper_path(options.program);
	if (path.empty())
	{
		result.failure = "helper program '" + options.program + "' is not installed";
		return result;
	}
	std::wstring wpath = widen(path);

	Handle token;
	Handle job;
	if (! make_token(token, result.failure) || ! make_job(job, options.memory_limit, result.failure))
		return result;

	Handle out_parent, out_child, err_parent, err_child, in_parent, in_child;
	Handle req_parent, req_child, rep_parent, rep_child;
	if (! make_pipe(out_parent, out_child, false, result.failure) ||
	    ! make_pipe(err_parent, err_child, false, result.failure) ||
	    ! make_pipe(in_parent, in_child, true, result.failure))
		return result;
	if (options.session &&
	    (! make_pipe(req_parent, req_child, false, result.failure) ||
	     ! make_pipe(rep_parent, rep_child, true, result.failure)))
		return result;

	// Command line: program name, then quoted arguments.
	std::wstring cmdline = quote_argument(widen(options.program));
	for (const std::string& a : options.args)
	{
		std::wstring w = widen(a);
		if (w.empty() && ! a.empty())
		{
			result.failure = "argument is not valid UTF-8";
			return result;
		}
		cmdline += L" " + quote_argument(w);
	}

	// Minimal environment.
	std::wstring env;
	auto add_env = [&env](const std::wstring& s) { env += s; env.push_back(L'\0'); };
	wchar_t sysroot[MAX_PATH] = {};
	GetEnvironmentVariableW(L"SystemRoot", sysroot, MAX_PATH);
	add_env(std::wstring(L"SystemRoot=") + sysroot);
	add_env(L"LANG=C");
	add_env(L"LC_ALL=C");
	if (options.session)
		add_env(L"WPARTED_BROKER=" +
		        std::to_wstring(reinterpret_cast<intptr_t>(rep_child.get())) + L"," +
		        std::to_wstring(reinterpret_cast<intptr_t>(req_child.get())));
	env.push_back(L'\0');

	// Inherit exactly these handles.
	std::vector<HANDLE> inherit = { in_child.get(), out_child.get(), err_child.get() };
	if (options.session)
	{
		inherit.push_back(req_child.get());
		inherit.push_back(rep_child.get());
	}
	SIZE_T attr_size = 0;
	InitializeProcThreadAttributeList(nullptr, 2, 0, &attr_size);
	std::vector<uint8_t> attr_buf(attr_size);
	LPPROC_THREAD_ATTRIBUTE_LIST attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attr_buf.data());
	if (! InitializeProcThreadAttributeList(attrs, 2, 0, &attr_size))
	{
		result.failure = "InitializeProcThreadAttributeList: " + win_error(GetLastError());
		return result;
	}
	DWORD64 mitigations = PROCESS_CREATION_MITIGATION_POLICY_EXTENSION_POINT_DISABLE_ALWAYS_ON |
	                      PROCESS_CREATION_MITIGATION_POLICY_STRICT_HANDLE_CHECKS_ALWAYS_ON |
	                      PROCESS_CREATION_MITIGATION_POLICY_IMAGE_LOAD_NO_REMOTE_ALWAYS_ON |
	                      PROCESS_CREATION_MITIGATION_POLICY_IMAGE_LOAD_NO_LOW_LABEL_ALWAYS_ON |
	                      PROCESS_CREATION_MITIGATION_POLICY_IMAGE_LOAD_PREFER_SYSTEM32_ALWAYS_ON;
	bool attrs_ok = UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit.data(),
	                                          inherit.size() * sizeof(HANDLE), nullptr, nullptr) &&
	                UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY, &mitigations,
	                                          sizeof(mitigations), nullptr, nullptr);
	if (! attrs_ok)
	{
		result.failure = "UpdateProcThreadAttribute: " + win_error(GetLastError());
		DeleteProcThreadAttributeList(attrs);
		return result;
	}

	STARTUPINFOEXW si = {};
	si.StartupInfo.cb = sizeof(si);
	si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
	si.StartupInfo.hStdInput  = in_child.get();
	si.StartupInfo.hStdOutput = out_child.get();
	si.StartupInfo.hStdError  = err_child.get();
	si.lpAttributeList = attrs;

	std::wstring dir = wpath.substr(0, wpath.find_last_of(L"\\/"));
	std::vector<wchar_t> cmd(cmdline.begin(), cmdline.end());
	cmd.push_back(L'\0');
	PROCESS_INFORMATION pi = {};
	BOOL created = CreateProcessAsUserW(token.get(), wpath.c_str(), cmd.data(), nullptr, nullptr, TRUE,
	                                    EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED | CREATE_NO_WINDOW |
	                                    CREATE_UNICODE_ENVIRONMENT,
	                                    &env[0], dir.c_str(), &si.StartupInfo, &pi);
	DWORD create_error = GetLastError();
	DeleteProcThreadAttributeList(attrs);
	if (! created)
	{
		result.failure = "unable to start " + options.program + ": " + win_error(create_error);
		return result;
	}
	Handle process(pi.hProcess);
	Handle thread(pi.hThread);
	if (! AssignProcessToJobObject(job.get(), process.get()))
	{
		result.failure = "AssignProcessToJobObject: " + win_error(GetLastError());
		TerminateProcess(process.get(), 1);
		return result;
	}

	// The child's ends belong to the child now.
	in_child.reset();
	out_child.reset();
	err_child.reset();
	req_child.reset();
	rep_child.reset();

	result.started = true;
	ResumeThread(thread.get());

	std::mutex output_mutex;
	std::thread out_thread(read_stream, out_parent.get(), std::ref(result.output), false,
	                       std::cref(options.on_output), std::ref(output_mutex));
	std::thread err_thread(read_stream, err_parent.get(), std::ref(result.error), true,
	                       std::cref(options.on_output), std::ref(output_mutex));
	std::thread broker_thread;
	if (options.session)
	{
		broker_thread = std::thread([&]()
		{
			Broker::Server server(*options.session);
			HandleChannel channel(req_parent.get(), rep_parent.get());
			server.serve(channel);
			result.broker = server.statistics();
			if (server.statistics().refused)
				result.broker_error = server.last_error();
		});
	}

	if (! options.input.empty())
	{
		DWORD written = 0;
		WriteFile(in_parent.get(), options.input.data(), static_cast<DWORD>(options.input.size()), &written, nullptr);
	}
	in_parent.reset();

	ULONGLONG start = GetTickCount64();
	for (;;)
	{
		DWORD w = WaitForSingleObject(process.get(), 100);
		if (w == WAIT_OBJECT_0)
			break;
		if (options.cancelled && options.cancelled())
		{
			result.cancelled = true;
			TerminateJobObject(job.get(), 1);
		}
		else if (options.timeout_ms && GetTickCount64() - start > options.timeout_ms)
		{
			result.timed_out = true;
			TerminateJobObject(job.get(), 1);
		}
	}

	out_thread.join();
	err_thread.join();
	if (broker_thread.joinable())
	{
		// The helper is gone, so the broker sees end of stream; closing our ends
		// unblocks it in any case.
		broker_thread.join();
	}
	DWORD code = 1;
	GetExitCodeProcess(process.get(), &code);
	result.exit_code = static_cast<int>(code);
	return result;
}

#else  /* ! _WIN32 */

std::string helper_path(const std::string& name)
{
	(void)valid_helper_name(name);
	return std::string();
}


Result run(const Options& options)
{
	Result result;
	result.failure = "helper programs are only run on Windows";
	(void)options;
	return result;
}

#endif /* _WIN32 */


}  // namespace WinSandbox
}  // namespace WParted
