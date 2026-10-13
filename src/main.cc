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

#include <glibmm.h>
#include <gtkmm/messagedialog.h>
#include <gtkmm/main.h>
#include <iostream>
#include <stdlib.h>


#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
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
#else
#include <unistd.h>
static bool is_running_as_admin()
{
	return getuid() == 0;
}
#endif

int main( int argc, char *argv[] )
{
	WParted::WParted_Core::mainthread = Glib::Thread::self();

	Gtk::Main kit( argc, argv ) ;

	// Match the wparted.desktop filename so Gtk finds and provides the localised name
	// and correct icon to the Wayland compositor.
	Glib::set_prgname("wparted");
	// Set WM_CLASS X Window property for correct naming on X11.
	gdk_set_program_class( "WParted" ) ;

	//i18n
	bindtextdomain( GETTEXT_PACKAGE, GNOMELOCALEDIR ) ;
	bind_textdomain_codeset( GETTEXT_PACKAGE, "UTF-8" ) ;
	textdomain( GETTEXT_PACKAGE ) ;

	// Display version and configuration info when starting for command line users.
	std::cout << WParted::WParted_Core::get_version_and_config_string() << std::endl;

	//check privileges
	if ( !is_running_as_admin() )
	{
		const Glib::ustring error_msg(_("Administrator privileges are required for running WParted"));
		std::cerr << error_msg << std::endl;

		Gtk::MessageDialog dialog(error_msg, false, Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK);
		dialog .set_secondary_text(
				_("Since WParted is a powerful tool capable of modifying partition tables and raw storage volumes, Administrator privileges are required.") ) ;
		
		dialog .run() ;
		exit(EXIT_FAILURE);
	}

	//deal with arguments..
	std::vector<Glib::ustring> user_devices(argv + 1, argv + argc);
	
	WParted::Win_WParted win_gparted( user_devices ) ; 
	Gtk::Main::run( win_gparted ) ;

	return EXIT_SUCCESS;
}
