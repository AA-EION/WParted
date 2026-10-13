/* WParted — Native Windows Partition Editor (Forked from GParted)
 * Copyright (C) 2026 WParted Contributors
 *
 * Licensed under the GNU General Public License version 2 or later.
 */

#ifndef WPARTED_WIN32_VOLUME_LOCK_GUARD_H
#define WPARTED_WIN32_VOLUME_LOCK_GUARD_H

#include <string>
#include <vector>
#include <cstdint>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
typedef void* HANDLE;
#endif

namespace WParted
{

struct LockingProcessInfo
{
    uint32_t processId;
    std::wstring appName;
    std::wstring serviceShortName;
};

class Win32VolumeLockGuard
{
public:
    Win32VolumeLockGuard();
    explicit Win32VolumeLockGuard(const std::wstring& volumePath);
    ~Win32VolumeLockGuard();

    // Non-copyable, movable
    Win32VolumeLockGuard(const Win32VolumeLockGuard&) = delete;
    Win32VolumeLockGuard& operator=(const Win32VolumeLockGuard&) = delete;
    Win32VolumeLockGuard(Win32VolumeLockGuard&& other) noexcept;
    Win32VolumeLockGuard& operator=(Win32VolumeLockGuard&& other) noexcept;

    // Acquire lock on volume
    bool Lock(const std::wstring& volumePath, uint32_t maxRetries = 5, uint32_t retryDelayMs = 500);

    // Dismount volume
    bool Dismount();

    // Enable extended DASD I/O for raw sector writes past boundary
    bool AllowExtendedDasdIo();

    // Manually release lock before destruction
    void Unlock();

    // Status queries
    bool IsLocked() const { return m_isLocked; }
    bool IsDismounted() const { return m_isDismounted; }
    HANDLE GetHandle() const { return m_hVolume; }
    const std::wstring& GetVolumePath() const { return m_volumePath; }
    const std::vector<LockingProcessInfo>& GetLockingProcesses() const { return m_lockingProcesses; }

    // Discover processes preventing volume locking via Restart Manager API
    static std::vector<LockingProcessInfo> QueryLockingProcesses(const std::wstring& volumePath);

private:
    HANDLE m_hVolume;
    std::wstring m_volumePath;
    bool m_isLocked;
    bool m_isDismounted;
    std::vector<LockingProcessInfo> m_lockingProcesses;
};

} // namespace WParted

#endif // WPARTED_WIN32_VOLUME_LOCK_GUARD_H
