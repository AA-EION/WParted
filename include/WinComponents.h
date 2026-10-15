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

/* WinComponents
 *
 * What this installation of WParted has: the bundled helper programs, and
 * the optional components (WSL), as input for the capability resolver.
 */

#ifndef WPARTED_WINCOMPONENTS_H
#define WPARTED_WINCOMPONENTS_H

#include "WinCapabilities.h"


namespace WParted
{
namespace WinComponents
{


// Detected once, then cached until refresh().
Caps::Components current();
void refresh();

// The user's choice in Preferences.
void set_experimental(bool on);


}  // namespace WinComponents
}  // namespace WParted

#endif /* WPARTED_WINCOMPONENTS_H */
