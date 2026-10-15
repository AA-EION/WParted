/* Copyright (C) 2004 Bart
 * Copyright (C) 2026 WParted contributors (Windows port)
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
#include "WinDevNames.h"
#include "Device.h"
#include "Utils.h"
#include <gtkmm/icontheme.h>
#include <memory>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdio>
#else
#include <unistd.h>
#endif


#if defined(_WIN32)
// The executable manifest requires administrator rights, so Windows always starts
// WParted elevated.  Check anyway: with UAC disabled a standard user would get an
// unelevated process.
static bool running_elevated()
{
	HANDLE token = nullptr;
	if (! OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
		return false;
	TOKEN_ELEVATION elevation;
	DWORD size = 0;
	bool elevated = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size) &&
	                elevation.TokenIsElevated;
	CloseHandle(token);
	return elevated;
}


// Show text output when started from a console (wparted --version).
static void attach_parent_console()
{
	// Keep output already redirected to a file or pipe (e.g. in CI).
	DWORD type = GetFileType(GetStdHandle(STD_OUTPUT_HANDLE));
	if (type == FILE_TYPE_DISK || type == FILE_TYPE_PIPE)
		return;
	if (AttachConsole(ATTACH_PARENT_PROCESS))
	{
		FILE* f = nullptr;
		freopen_s(&f, "CONOUT$", "w", stdout);
		freopen_s(&f, "CONOUT$", "w", stderr);
	}
}


static void print_partition(const WParted::Partition& p, const char* indent)
{
	std::cout << indent << p.get_path() << "  " << WParted::Utils::get_filesystem_string(p.fstype)
	          << "  sectors " << p.sector_start << "-" << p.sector_end
	          << (p.busy ? "  busy" : "");
	for (const Glib::ustring& mp : p.get_mountpoints())
		std::cout << "  [" << mp << "]";
	if (! p.get_filesystem_label().empty())
		std::cout << "  label=\"" << p.get_filesystem_label() << "\"";
	for (const Glib::ustring& f : p.get_flags())
		std::cout << "  flag:" << f;
	std::cout << "\n";
	for (const Glib::ustring& m : p.get_messages())
		std::cout << indent << "    message: " << m << "\n";
	for (unsigned int i = 0; i < p.logicals.size(); i++)
		print_partition(p.logicals[i], "      ");
}


// --smoke-test: read only device scan report plus a short display of the main
// window.  Used by CI on a real Windows machine to check the bundled GTK runtime
// and the Windows disk layer on real disks.
static int smoke_test()
{
	std::cout << WParted::WParted_Core::get_version_and_config_string() << std::endl;
	Glib::RefPtr<Gtk::IconTheme> theme = Gtk::IconTheme::get_default();
	bool icons_ok = theme->has_icon("wparted") && theme->has_icon("dialog-warning");
	std::cout << "icon theme: wparted=" << theme->has_icon("wparted")
	          << " dialog-warning=" << theme->has_icon("dialog-warning") << std::endl;

	WParted::WParted_Core core;
	core.set_user_devices(std::vector<Glib::ustring>());  // Probe all disks, as the GUI does
	std::vector<std::unique_ptr<WParted::Device>> devices;
	core.set_devices(devices);
	for (const auto& d : devices)
	{
		std::cout << d->get_path() << "  " << d->model << "  " << d->length << " sectors of "
		          << d->sector_size << " bytes  table=" << d->disktype
		          << (d->readonly ? "  READ-ONLY" : "") << "\n";
		for (unsigned int i = 0; i < d->partitions.size(); i++)
			print_partition(d->partitions[i], "   ");
	}
	std::cout << devices.size() << " device(s)" << std::endl;

	std::vector<Glib::ustring> none;
	WParted::Win_WParted window(none);
	Glib::signal_timeout().connect_seconds([&window]() -> bool
	{
		window.hide();
		Gtk::Main::quit();
		return false;
	}, 15);
	Gtk::Main::run(window);
	std::cout << "main window shown and closed" << std::endl;
	// Any Windows machine has at least its system disk.
	return icons_ok && ! devices.empty() ? EXIT_SUCCESS : EXIT_FAILURE;
}
#endif


int main( int argc, char *argv[] )
{
#if defined(_WIN32)
	bool smoke = false;
	for (int i = 1; i < argc; i++)
	{
		Glib::ustring arg = argv[i];
		if (arg == "--smoke-test")
		{
			attach_parent_console();
			smoke = true;
			continue;
		}
		if (arg == "--version" || arg == "--help" || arg == "-h" || arg == "/?")
		{
			attach_parent_console();
			std::cout << "WParted " << VERSION << " - Windows Partition Editor\n"
			          << "A Windows port of GParted, the GNOME Partition Editor.\n\n";
			if (arg != "--version")
				std::cout << "Usage: wparted [DEVICE...]\n\n"
				          << "DEVICE   Show only these disks, e.g. /dev/sdb or \\\\.\\PhysicalDrive1\n";
			std::cout << std::flush;
			return EXIT_SUCCESS;
		}
	}
#endif

	WParted::WParted_Core::mainthread = Glib::Thread::self();

	Gtk::Main kit( argc, argv ) ;

	// Match the wparted.desktop filename so Gtk finds and provides the localised name
	// and correct icon to the Wayland compositor.
	Glib::set_prgname("wparted");
	// Set WM_CLASS X Window property for correct naming on X11.
	gdk_set_program_class( "WParted" ) ;

	//i18n
#if defined(_WIN32)
	// Translations are installed relative to the executable: <prefix>\share\locale
	{
		gchar* prefix = g_win32_get_package_installation_directory_of_module(nullptr);
		gchar* localedir = g_build_filename(prefix, "share", "locale", nullptr);
		bindtextdomain( GETTEXT_PACKAGE, localedir ) ;
		g_free(localedir);
		g_free(prefix);
	}
#else
	bindtextdomain( GETTEXT_PACKAGE, GNOMELOCALEDIR ) ;
#endif
	bind_textdomain_codeset( GETTEXT_PACKAGE, "UTF-8" ) ;
	textdomain( GETTEXT_PACKAGE ) ;

	// Display version and configuration info when starting for command line users.
	std::cout << WParted::WParted_Core::get_version_and_config_string() << std::endl;

	//check privileges
#if defined(_WIN32)
	if ( ! running_elevated() )
	{
		const Glib::ustring error_msg(_("Administrator privileges are required for running WParted"));
		std::cerr << error_msg << std::endl;

		Gtk::MessageDialog dialog(error_msg, false, Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK);
		dialog .set_secondary_text(
				_("Since WParted is a powerful tool capable of destroying partition tables and vast amounts of data, it only runs as an elevated administrator.  Start it with \"Run as administrator\".") ) ;
		dialog .run() ;
		exit(EXIT_FAILURE);
	}
#else
	if ( getuid() != 0 )
	{
		const Glib::ustring error_msg(_("Root privileges are required for running GParted"));
		std::cerr << error_msg << std::endl;

		Gtk::MessageDialog dialog(error_msg, false, Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK);
		dialog .set_secondary_text(
				_("Since GParted is a powerful tool capable of destroying partition tables and vast amounts of data, only root may run it.") ) ;

		dialog .run() ;
		exit(EXIT_FAILURE);
	}
#endif

#if defined(_WIN32)
	if (smoke)
		return smoke_test();
#endif

	//deal with arguments..
	std::vector<Glib::ustring> user_devices;
	for (int i = 1; i < argc; i++)
	{
#if defined(_WIN32)
		// Accept both \\.\PhysicalDriveN and /dev/sdX, always use /dev/sdX inside.
		int index = WParted::WinDevNames::disk_index(argv[i]);
		if (index < 0)
		{
			std::cerr << "Ignoring unknown device name: " << argv[i] << std::endl;
			continue;
		}
		user_devices.push_back(WParted::WinDevNames::disk_path(index));
#else
		user_devices.push_back(argv[i]);
#endif
	}

	WParted::Win_WParted win_wparted( user_devices ) ;
	Gtk::Main::run( win_wparted ) ;

	return EXIT_SUCCESS;
}
