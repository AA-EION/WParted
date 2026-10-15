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

#include "WinHelpers.h"
#include "WinDiskBackend.h"
#include "WinDiskModel.h"

#include <algorithm>
#include <memory>

#if defined(_WIN32)
#include <windows.h>
#endif


namespace WParted
{
namespace WinHelpers
{


const char* const DEVICE = "wparted-broker";


const std::vector<std::string>& tool_names()
{
	static const std::vector<std::string> names = { "mke2fs", "e2fsck", "tune2fs", "dumpe2fs", "resize2fs" };
	return names;
}


bool tools_installed()
{
	for (const std::string& name : tool_names())
		if (WinSandbox::helper_path(name).empty())
			return false;
	return true;
}


bool locate(int disk, uint64_t offset, Target& target, std::string& error)
{
	WinDisk::DiskBackend* backend = WinDisk::get_backend();
	if (! backend)
	{
		error = "No disk access";
		return false;
	}
	WinDisk::DiskLayout layout;
	if (! backend->read_layout(disk, layout, error))
		return false;
	for (const WinDisk::LayoutEntry& e : layout.entries)
	{
		if (e.offset == offset && WinDisk::holds_data(layout, e))
		{
			target.disk = disk;
			target.offset = e.offset;
			target.length = e.length;
			return true;
		}
	}
	error = "No partition starts at byte " + std::to_string(offset) + " of disk " + std::to_string(disk);
	return false;
}


namespace
{

bool has(const Command& c, const char* arg)
{
	return std::find(c.args.begin(), c.args.end(), arg) != c.args.end();
}


// Tools and options that only read.
bool reads_only(const Command& c)
{
	if (c.program == "dumpe2fs")
		return true;
	if (c.program == "e2fsck")
		return has(c, "-n");
	if (c.program == "resize2fs")
		return has(c, "-P");
	if (c.program == "tune2fs")
		return c.args.size() == 2 && c.args[0] == "-l";
	return false;
}


std::string quote(const std::string& s)
{
	if (! s.empty() && s.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.,/:=+%@")
	                   == std::string::npos)
		return s;
	std::string q = "'";
	for (char ch : s)
	{
		if (ch == '\'')
			q += "'\\''";
		else
			q += ch;
	}
	return q + "'";
}


uint64_t memory_limit_for(const Command& c)
{
	const uint64_t GiB = 1024ULL * 1024 * 1024;
	if (c.program != "e2fsck" && c.program != "resize2fs")
		return 1 * GiB;
	// Checking and resizing large file systems needs memory in proportion to
	// their size.  Allow up to three quarters of the physical memory.
	uint64_t limit = 8 * GiB;
#if defined(_WIN32)
	MEMORYSTATUSEX ms = {};
	ms.dwLength = sizeof(ms);
	if (GlobalMemoryStatusEx(&ms) && ms.ullTotalPhys > 0)
		limit = std::max<uint64_t>(2 * GiB, ms.ullTotalPhys / 4 * 3);
#endif
	return limit;
}

}  // anonymous namespace


bool validate(const Command& c, std::string& error)
{
	const std::vector<std::string>& names = tool_names();
	if (std::find(names.begin(), names.end(), c.program) == names.end())
	{
		error = "Not a bundled tool: " + c.program;
		return false;
	}
	if (std::count(c.args.begin(), c.args.end(), std::string(DEVICE)) != 1)
	{
		error = "The command must name the partition exactly once";
		return false;
	}
	if (c.target.disk < 0 || c.target.length == 0)
	{
		error = "No partition given";
		return false;
	}
	if (c.writable && reads_only(c))
	{
		error = "A reading command must not get write access";
		return false;
	}
	if (! c.writable && ! reads_only(c))
	{
		error = "This command needs write access";
		return false;
	}
	return true;
}


std::string display(const Command& c, const std::string& device_path)
{
	std::string s = c.program;
	for (const std::string& a : c.args)
		s += " " + quote(a == DEVICE ? device_path : a);
	return s;
}


WinSandbox::Result run(const Command& c, OutputSlot on_output, CancelSlot cancelled)
{
	WinSandbox::Result result;
	if (! validate(c, result.failure))
		return result;
	WinDisk::DiskBackend* backend = WinDisk::get_backend();
	if (! backend)
	{
		result.failure = "No disk access";
		return result;
	}
	std::unique_ptr<WinDisk::DiskSession> session =
	        backend->open_session(c.target.disk, c.target.offset, c.target.length, c.writable,
	                              WinDisk::SessionRange::INSIDE_PARTITION, result.failure);
	if (! session)
		return result;

	// Configuration only from WParted's own installation directory (writable by
	// administrators only), never from \etc\ on the current drive, which any user
	// can create.  e2fsck.conf, blkid.conf and blkid.tab don't exist there: the
	// tools then use their built-in defaults, as without the files on Linux.
	std::string path = WinSandbox::helper_path(c.program);
	std::string dir = path.substr(0, path.find_last_of("\\/") + 1);
	WinSandbox::Options o;
	o.program = c.program;
	o.args = c.args;
	o.environment = { "MKE2FS_CONFIG=" + dir + "mke2fs.conf", "E2FSCK_CONFIG=" + dir + "e2fsck.conf",
	                  "BLKID_CONF=" + dir + "blkid.conf", "BLKID_FILE=" + dir + "blkid.tab" };
	o.session = session.get();
	o.timeout_ms = c.timeout_ms;
	o.memory_limit = memory_limit_for(c);
	o.on_output = on_output;
	o.cancelled = cancelled;
	result = WinSandbox::run(o);

	std::string flush_error;
	if (c.writable && ! session->flush(flush_error) && result.failure.empty())
		result.broker_error = "flush: " + flush_error;
	return result;
}


namespace Ext
{

namespace
{

Command make(const Target& t, const char* program, std::vector<std::string> args, bool writable)
{
	Command c;
	c.program = program;
	c.args = std::move(args);
	c.target = t;
	c.writable = writable;
	return c;
}

}  // anonymous namespace


Command create(const Target& t, const std::string& fstype, const std::string& label)
{
	// GParted: mkfs.<fstype> -F -L <label> <device>
	return make(t, "mke2fs", { "-F", "-t", fstype, "-L", label, DEVICE }, true);
}


Command check_repair(const Target& t)
{
	return make(t, "e2fsck", { "-f", "-y", "-v", "-C", "0", DEVICE }, true);
}


Command check_read_only(const Target& t)
{
	return make(t, "e2fsck", { "-f", "-n", DEVICE }, false);
}


Command info(const Target& t)
{
	Command c = make(t, "dumpe2fs", { "-h", DEVICE }, false);
	c.timeout_ms = 60000;
	return c;
}


Command min_size(const Target& t)
{
	Command c = make(t, "resize2fs", { "-P", DEVICE }, false);
	c.timeout_ms = 120000;
	return c;
}


Command set_label(const Target& t, const std::string& label)
{
	// GParted runs e2label, which is tune2fs -L.
	return make(t, "tune2fs", { "-L", label, DEVICE }, true);
}


Command new_uuid(const Target& t)
{
	return make(t, "tune2fs", { "-U", "random", DEVICE }, true);
}


Command resize(const Target& t, uint64_t size_kib, bool convert_to_64bit)
{
	std::vector<std::string> args = { "-p" };
	if (convert_to_64bit)
		args.push_back("-b");
	args.push_back(DEVICE);
	if (size_kib)
		args.push_back(std::to_string(size_kib) + "K");
	return make(t, "resize2fs", args, true);
}

}  // namespace Ext


}  // namespace WinHelpers
}  // namespace WParted
