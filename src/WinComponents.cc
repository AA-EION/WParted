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

#include "WinComponents.h"
#include "WinHelpers.h"
#include "WinSandbox.h"

#include <mutex>


namespace WParted
{
namespace WinComponents
{


namespace
{

std::mutex       g_mutex;
bool             g_detected = false;
bool             g_experimental = false;
Caps::Components g_components;


void detect()
{
	Caps::Components c;
	c.wparted_tools = WinHelpers::tools_installed();
	c.file_reader = ! WinSandbox::helper_path("wparted-applefs").empty();
	c.experimental = g_experimental;
	c.block_copy = true;   // Verified block copy (WinCopy.h)
	g_components = c;
	g_detected = true;
}

}  // anonymous namespace


Caps::Components current()
{
	std::lock_guard<std::mutex> lock(g_mutex);
	if (! g_detected)
		detect();
	return g_components;
}


void refresh()
{
	std::lock_guard<std::mutex> lock(g_mutex);
	detect();
}


void set_experimental(bool on)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	g_experimental = on;
	g_components.experimental = on;
}


}  // namespace WinComponents
}  // namespace WParted
