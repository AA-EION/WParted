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

/* WinHelpers
 *
 * The bundled e2fsprogs tools (mke2fs, e2fsck, tune2fs, dumpe2fs, resize2fs,
 * built by helpers/e2fsprogs/build.sh) and how WParted runs them on Windows:
 *
 *   - only these programs, from WParted's own installation directory;
 *   - an argument vector built by WParted, never a command line or a shell;
 *   - in the sandbox (WinSandbox.h), reaching exactly one partition through
 *     the broker, read-only unless the operation writes;
 *   - the partition as it is on disk now, looked up in the live partition
 *     table: the helper sees the same device a Linux tool would.
 */

#ifndef WPARTED_WINHELPERS_H
#define WPARTED_WINHELPERS_H

#include "WinSandbox.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>


namespace WParted
{
namespace WinHelpers
{


// The device name the helpers accept (helpers/broker/wpbroker.h).
extern const char* const DEVICE;


struct Target
{
	int      disk = -1;       // PhysicalDrive number
	uint64_t offset = 0;      // Bytes
	uint64_t length = 0;      // Bytes
};


struct Command
{
	std::string              program;   // One of tool_names()
	std::vector<std::string> args;      // Exactly one of them is DEVICE
	Target                   target;
	bool                     writable = false;
	uint32_t                 timeout_ms = 0;
};


const std::vector<std::string>& tool_names();
// All tools are installed next to WParted.
bool tools_installed();

// Find the partition starting at 'offset' on 'disk' in the partition table as it
// is on disk now.
bool locate(int disk, uint64_t offset, Target& target, std::string& error);

// Refuse what WParted never runs: unknown programs, no or several device
// arguments, writable sessions for reading tools.
bool validate(const Command& command, std::string& error);

// For the operation details: "mke2fs -F -t ext4 -L 'my disk' /dev/sdb2".
std::string display(const Command& command, const std::string& device_path);

typedef std::function<void(const std::string& chunk, bool is_stderr)> OutputSlot;
typedef std::function<bool()> CancelSlot;

// Validate and run.  On failure to start, the result says why (result.failure).
WinSandbox::Result run(const Command& command, OutputSlot on_output = OutputSlot(),
                       CancelSlot cancelled = CancelSlot());


// The commands GParted's ext2 class runs, with the bundled tools.
namespace Ext
{
	Command create(const Target& t, const std::string& fstype, const std::string& label);
	Command check_repair(const Target& t);                    // e2fsck -f -y -v -C 0
	Command check_read_only(const Target& t);                 // e2fsck -f -n
	Command info(const Target& t);                            // dumpe2fs -h
	Command min_size(const Target& t);                        // resize2fs -P
	Command set_label(const Target& t, const std::string& label);
	Command new_uuid(const Target& t);
	// 'size_kib' 0: fill the partition.
	Command resize(const Target& t, uint64_t size_kib, bool convert_to_64bit);
}


}  // namespace WinHelpers
}  // namespace WParted

#endif /* WPARTED_WINHELPERS_H */
