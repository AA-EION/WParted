/* WParted — Native Windows Partition Editor (Forked from GParted)
 * Copyright (C) 2026 WParted Contributors
 *
Copyright (C) 2023 Mike Fleetwood
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

/* common
 *
 * Common functions used in testing that don't depend on any GParted code so don't require
 * any GParted objects to need to be linked in order to be used.
 */

#ifndef WPARTED_TEST_COMMON_H
#define WPARTED_TEST_COMMON_H


#include <stddef.h>
#include <string>


namespace WParted
{


const size_t BinaryStringChunkSize = 16;

std::string binary_string_to_print(size_t offset, const char* s, size_t len);
void ensure_x11_display(int argc, char** argv);


}  // namespace WParted


#endif /* GPARTED_TEST_COMMON_H */
