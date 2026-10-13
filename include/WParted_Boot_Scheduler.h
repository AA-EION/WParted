/* WParted — Native Windows Partition Editor (Forked from GParted)
 * Copyright (C) 2026 WParted Contributors
 *
 * Licensed under the GNU General Public License version 2 or later.
 */

#ifndef WPARTED_BOOT_SCHEDULER_H
#define WPARTED_BOOT_SCHEDULER_H

#include "WParted_Boot_Job.h"

#include <vector>
#include <string>

namespace WParted
{

class WParted_Boot_Scheduler
{
public:
    // Determine whether an operation requires boot-time execution (e.g. active system C: drive)
    static bool IsBootAccessRequired(const std::wstring& driveLetterOrMountPoint, bool movingStartSector);

    // Schedule pending operations to run at next Windows reboot
    static bool ScheduleBootJob(const std::vector<BootJobOperation>& operations);

    // Cancel currently scheduled boot job and clean up BootExecute registry
    static bool CancelBootJob();

    // Check if a boot job is currently registered in BootExecute
    static bool IsBootJobScheduled();

    // Get absolute path to the serialization job file (C:\Windows\System32\wparted-jobs.bin)
    static std::wstring GetJobFilePath();
};

} // namespace WParted

#endif // WPARTED_BOOT_SCHEDULER_H
