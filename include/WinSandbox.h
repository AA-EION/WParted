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

/* WinSandbox
 *
 * Runs one bundled helper program with:
 *   - a restricted token: Administrators group deny-only, all privileges
 *     removed, low integrity level, so it can not open disks, volumes or most
 *     of the file system;
 *   - a job object: killed with WParted, one process only, memory limit, no
 *     access to the clipboard, desktop settings or other processes' windows;
 *   - only the handles it is given (broker pipes, stdin, stdout, stderr);
 *   - a minimal environment.
 * Disk access goes through the broker (WinBroker.h) for the DiskSession given.
 */

#ifndef WPARTED_WINSANDBOX_H
#define WPARTED_WINSANDBOX_H

#include "WinBroker.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>


namespace WParted
{
namespace WinSandbox
{


struct Options
{
	std::string              program;       // Helper name, e.g. "mke2fs" (see helper_path())
	std::vector<std::string> args;          // argv[1..], UTF-8
	WinDisk::DiskSession*    session = nullptr;   // Disk access, or none
	std::string              input;         // Written to stdin, then closed
	std::vector<std::string> environment;   // Extra "NAME=value" entries, UTF-8
	uint32_t                 timeout_ms = 0;      // 0 = no limit
	uint64_t                 memory_limit = 2ULL * 1024 * 1024 * 1024;
	// Called from a worker thread with each chunk of output as it arrives.
	std::function<void(const std::string& chunk, bool is_stderr)> on_output;
	// Polled about ten times a second; returning true terminates the helper.
	std::function<bool()> cancelled;
	// Hardening measures, all on by default.  Only the integration tests switch
	// single measures off, to report which one a failing helper trips over.
	unsigned hardening = HARDEN_ALL;

	enum : unsigned
	{
		HARDEN_LOW_INTEGRITY = 1,
		HARDEN_DENY_ADMINS   = 2,
		HARDEN_JOB_UI_LIMITS = 4,
		HARDEN_MITIGATIONS   = 8,
		HARDEN_ALL           = 15,
	};
};


struct Result
{
	bool               started   = false;
	int                exit_code = -1;
	bool               timed_out = false;
	bool               cancelled = false;
	std::string        output;
	std::string        error;
	std::string        failure;       // Why the helper could not be started
	Broker::Statistics broker;
	std::string        broker_error;  // Last refused or failed broker request
};


// Absolute path of a bundled helper: <prefix>\lib\wparted\<name>.exe next to
// the running executable.  Empty when the helper is not installed.  Names are
// restricted to [A-Za-z0-9._-].
std::string helper_path(const std::string& name);

// Run a helper and wait for it.  Windows only; elsewhere returns failure.
Result run(const Options& options);

// Windows command line quoting (CommandLineToArgvW rules), exposed for tests.
std::wstring quote_argument(const std::wstring& arg);


}  // namespace WinSandbox
}  // namespace WParted

#endif /* WPARTED_WINSANDBOX_H */
