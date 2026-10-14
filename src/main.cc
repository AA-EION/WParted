/* WParted — Native Windows Partition Editor (Forked from GParted)
 * Copyright (C) 2026 WParted Contributors
 *
Copyright (C) 2004 Bart
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

#include "WParted_Core.h"
#include "Win_WParted.h"
#include "i18n.h"

#include <glibmm.h>
#include <gtkmm/messagedialog.h>
#include <gtkmm/main.h>
#include <iostream>
#include <stdlib.h>


#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <io.h>

static bool is_running_as_admin()
{
	BOOL is_admin = FALSE;
	PSID admin_group = NULL;
	SID_IDENTIFIER_AUTHORITY nt_auth = SECURITY_NT_AUTHORITY;
	if (AllocateAndInitializeSid(&nt_auth, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
	                             0, 0, 0, 0, 0, 0, &admin_group))
	{
		CheckTokenMembership(NULL, admin_group, &is_admin);
		FreeSid(admin_group);
	}
	return is_admin != FALSE;
}

static bool is_launched_from_explorer()
{
	DWORD procList[2] = { 0 };
	DWORD count = GetConsoleProcessList(procList, 2);
	return count <= 1;
}

static std::wstring Utf8ToWide(const std::string& str)
{
	if (str.empty()) return std::wstring();
	int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.length()), NULL, 0);
	if (size <= 0) return std::wstring();
	std::wstring out(size, 0);
	MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.length()), &out[0], size);
	return out;
}

static bool elevate_self(int argc, char* argv[], bool launchedFromExplorer)
{
	wchar_t exePath[MAX_PATH];
	if (!GetModuleFileNameW(NULL, exePath, MAX_PATH)) return false;

	std::wstring params;
	for (int i = 1; i < argc; ++i) {
		if (i > 1) params += L" ";
		std::wstring warg = Utf8ToWide(argv[i]);
		bool needQuotes = warg.empty() || (warg.find(L' ') != std::wstring::npos) || (warg.find(L'\t') != std::wstring::npos);
		if (needQuotes) params += L"\"";
		for (wchar_t c : warg) {
			if (c == L'"') params += L"\\\"";
			else params += c;
		}
		if (needQuotes) params += L"\"";
	}

	SHELLEXECUTEINFOW sei = { sizeof(sei) };
	sei.cbSize = sizeof(sei);
	sei.lpVerb = L"runas";
	sei.lpFile = exePath;
	sei.lpParameters = params.empty() ? NULL : params.c_str();
	sei.nShow = SW_SHOWNORMAL;
	sei.fMask = SEE_MASK_NOCLOSEPROCESS;

	if (ShellExecuteExW(&sei)) {
		if (launchedFromExplorer) {
			if (sei.hProcess) CloseHandle(sei.hProcess);
			exit(0);
		} else {
			if (sei.hProcess) {
				WaitForSingleObject(sei.hProcess, INFINITE);
				DWORD exitCode = 0;
				GetExitCodeProcess(sei.hProcess, &exitCode);
				CloseHandle(sei.hProcess);
				exit(exitCode);
			}
			exit(0);
		}
		return true;
	}
	return false;
}
#else
#include <unistd.h>
static bool is_running_as_admin()
{
	return getuid() == 0;
}
#endif

int main( int argc, char *argv[] )
{
#if defined(_WIN32)
	// Check if standard handles are already redirected (e.g. pipe or file)
	HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
	DWORD stdoutType = (hStdOut != NULL && hStdOut != INVALID_HANDLE_VALUE) ? GetFileType(hStdOut) : FILE_TYPE_UNKNOWN;
	bool stdoutRedirected = (stdoutType == FILE_TYPE_DISK || stdoutType == FILE_TYPE_PIPE || stdoutType == FILE_TYPE_CHAR);
	if (!stdoutRedirected) {
		if (AttachConsole(ATTACH_PARENT_PROCESS)) {
			freopen("CONOUT$", "w", stdout);
			freopen("CONOUT$", "w", stderr);
			freopen("CONIN$", "r", stdin);
		}
	}
	std::ios::sync_with_stdio(true);
#endif

	bool test_gui = false;
	bool no_elevate = false;
	std::vector<Glib::ustring> user_devices;

	for (int i = 1; i < argc; ++i) {
		std::string arg = argv[i];
		if (arg == "--help" || arg == "-h" || arg == "/?" || arg == "-?") {
			std::cout << "WParted 1.0.0 — Native Windows Partition Editor (GTKmm GUI)\n"
			          << "Usage: wparted [DEVICE...] [OPTIONS]\n\n"
			          << "Launches the graphical partition editor interface on Windows.\n"
			          << "Optional arguments:\n"
			          << "  DEVICE         Path to device to inspect, e.g. /dev/sda or \\\\.\\PhysicalDrive0\n"
			          << "  --no-elevate   Do not prompt for UAC administrator elevation (read-only mode)\n"
			          << "  --read-only    Launch GUI with standard user privileges in read-only mode\n"
			          << "  --test-gui     Initialize GTKmm GUI and exit cleanly after rendering\n"
			          << "  --help, -h     Show this help screen\n"
			          << "  --version      Show version information\n" << std::flush;
			return EXIT_SUCCESS;
		}
		if (arg == "--version" || arg == "-v") {
			std::cout << "WParted 1.0.0 (GTKmm3 Native Windows Interface)\n" << std::flush;
			return EXIT_SUCCESS;
		}
		if (arg == "--test-gui") {
			test_gui = true;
			no_elevate = true;
		} else if (arg == "--no-elevate" || arg == "--read-only") {
			no_elevate = true;
		} else {
			user_devices.push_back(arg);
		}
	}

	// Dynamic elevation for non-admin invocations unless explicitly bypassed
	if ( !no_elevate && !is_running_as_admin() )
	{
#if defined(_WIN32)
		bool launchedFromExplorer = is_launched_from_explorer();
		if (elevate_self(argc, argv, launchedFromExplorer)) {
			return EXIT_SUCCESS;
		}
		// If elevation is cancelled or unavailable, fall through to launching GUI in read-only mode
#endif
	}

	WParted::WParted_Core::mainthread = Glib::Thread::self();

	Gtk::Main kit( argc, argv ) ;

	// Match the wparted.desktop filename so Gtk finds and provides the localised name
	// and correct icon
	Glib::set_prgname("wparted");
	gdk_set_program_class( "WParted" ) ;

	//i18n
	bindtextdomain( GETTEXT_PACKAGE, GNOMELOCALEDIR ) ;
	bind_textdomain_codeset( GETTEXT_PACKAGE, "UTF-8" ) ;
	textdomain( GETTEXT_PACKAGE ) ;

	// Display version and configuration info when starting for command line users.
	std::cout << WParted::WParted_Core::get_version_and_config_string() << std::endl;

	// Check privileges
	if ( !is_running_as_admin() )
	{
		const Glib::ustring notice_msg(_("Notice: Running with standard user privileges (read-only mode)."));
		std::cout << notice_msg << "\n"
		          << _("Administrator privileges are required for modifying partition tables.") << std::endl;
	}

	WParted::Win_WParted win_gparted( user_devices ) ; 

	if (test_gui) {
		Glib::signal_timeout().connect_seconds([&win_gparted]() -> bool {
			win_gparted.hide();
			Gtk::Main::quit();
			return false;
		}, 2);
	}

	Gtk::Main::run( win_gparted ) ;

	return EXIT_SUCCESS;
}
