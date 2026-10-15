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

/* WinCapabilities
 *
 * The single answer to "can WParted do this here, who does it, and if not,
 * why not".  Every part of the user interface that enables, disables or
 * explains an operation asks resolve().
 *
 * Policy (doc/DESIGN-filesystems.md):
 *   - Windows formats: only Windows.  Never WSL, never helper programs.
 *   - Linux formats: WParted's own sandboxed tools where they exist (ext2/3/4,
 *     linux-swap), WSL for the rest; WSL only on disks it can attach.
 *   - macOS formats: read-only, always.
 *   - Moving and copying partitions: WParted's verified block copy.
 *
 * Reasons and fixes are constant English strings, marked for translation; the
 * user interface translates them when showing them.  'detail' carries extra,
 * untranslated information such as the name of a volume in use.
 */

#ifndef WPARTED_WINCAPABILITIES_H
#define WPARTED_WINCAPABILITIES_H

#include <string>
#include <vector>


namespace WParted
{
namespace Caps
{


enum class Op
{
	INFO,       // Read usage, label, UUID
	CREATE,     // Create (format) a file system
	CHECK,      // Check and repair
	LABEL,      // Change the label
	UUID,       // Change the UUID / serial number
	GROW,
	SHRINK,
	MOVE,       // Move the start of the partition
	COPY,       // Copy the partition and its contents
	BROWSE,     // Open the files read-only inside WParted (copy them out)
	OPEN_RW,    // Open the files read/write (WSL)
};
const std::vector<Op>& all_ops();
const char* op_name(Op op);    // Translatable, e.g. "Check"


enum class Provider
{
	NONE,
	WINDOWS,    // Windows itself
	WPARTED,    // WParted, or its sandboxed helper programs
	WSL,        // WParted's Linux environment in WSL 2
};
const char* provider_name(Provider p);   // "Windows", "WParted", "WSL"


enum class State
{
	AVAILABLE,
	NEEDS_COMPONENT,     // Possible once something is installed or switched on
	NOT_FOR_THIS_DISK,   // Possible elsewhere, not for this disk or partition
	UNSUPPORTED,         // Not possible on Windows
	READ_ONLY,           // macOS file systems are read-only by design
	NOT_APPLICABLE,      // The operation means nothing here (checking swap space)
};


enum class Family
{
	WINDOWS,
	LINUX,
	APPLE,
	OTHER,
	NONE,      // Unallocated, extended, unknown
};
// File system names as used by GParted (Utils::get_filesystem_string()):
// "ntfs", "fat32", "ext4", "linux-swap", "hfs+", "apfs", ...
Family family_of(const std::string& fs);


struct Components
{
	bool        wparted_tools = false;    // Bundled helpers for ext2/3/4 and swap
	bool        file_reader = false;      // Bundled read-only reader (HFS+, APFS, ext2/3/4)
	bool        wsl_usable = false;       // wsl.exe present, WSL 2 with --mount
	std::string wsl_problem;              // Why not, when known
	bool        wsl_distro_ready = false; // WParted-Linux imported and verified
	std::vector<std::string> wsl_kernel_fs;  // File systems the WSL kernel offers (empty: assume ext4/xfs/btrfs/f2fs)
	bool        experimental = false;     // Experimental features switched on
	bool        block_copy = false;       // This WParted moves and copies partitions
};


struct DiskContext
{
	bool        known = false;            // false: answer for the file system in general
	bool        read_only = false;        // WParted treats the disk as read-only
	std::string read_only_reason;
	bool        system_disk = false;      // Windows is installed on this disk
	bool        removable = false;        // USB flash drive, memory card
	bool        other_volumes_in_use = false;  // Windows volumes on the same disk are in use
	std::string other_volumes;            // Their names, for the message
	bool        partition_busy = false;   // This partition is in use by Windows
	std::string busy_reason;
};


struct Capability
{
	State       state = State::UNSUPPORTED;
	Provider    provider = Provider::NONE;
	bool        experimental = false;
	const char* reason = "";      // Translatable
	const char* fix = "";         // Translatable, may be empty
	std::string detail;           // Untranslated extra information

	bool available() const  { return state == State::AVAILABLE; }
};


Capability resolve(const std::string& fs, Op op, const DiskContext& disk, const Components& comps);


}  // namespace Caps
}  // namespace WParted

#endif /* WPARTED_WINCAPABILITIES_H */
