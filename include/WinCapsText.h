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

/* Translated, human readable explanations of the capability resolver's
 * answers (WinCapabilities.h), for the File System Support and Partition
 * Information dialogs.
 */

#ifndef WPARTED_WINCAPSTEXT_H
#define WPARTED_WINCAPSTEXT_H

#if defined(_WIN32)

#include "WinCapabilities.h"
#include "Utils.h"

#include <glibmm/ustring.h>


namespace WParted
{
namespace WinCapsText
{


// "Create, Check, Label"
Glib::ustring ops(const std::vector<Caps::Op>& ops);

// One line per provider and per reason, e.g.
//   Done by WParted: Read usage
//   Not available (Create, Check, ...): Working through WSL is experimental. ...
Glib::ustring explain(FSType fstype, const Caps::DiskContext& disk);

// Short: "Windows", "WParted", "WParted, WSL (experimental)", "Read-only", "—"
Glib::ustring providers(FSType fstype);

// The bundled and optional components and their state, one per line.
Glib::ustring components();


}  // namespace WinCapsText
}  // namespace WParted

#endif /* _WIN32 */

#endif /* WPARTED_WINCAPSTEXT_H */
