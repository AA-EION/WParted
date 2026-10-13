/* WParted — Native Windows Partition Editor (Forked from GParted)
 * Copyright (C) 2026 WParted Contributors
 *
 * Licensed under the GNU General Public License version 2 or later.
 */

#ifndef WPARTED_BITLOCKER_GUARD_H
#define WPARTED_BITLOCKER_GUARD_H

#include <string>
#include <cstdint>

namespace WParted
{

struct BitLockerStatus
{
    bool isEncrypted = false;
    bool isLocked = false;
    bool isProtectionOn = false;
    uint32_t encryptionPercentage = 0;
    std::string statusString;
};

class BitLockerGuard
{
public:
    // Query BitLocker status for a given volume drive letter (e.g. L"C:" or L"C:\\")
    static BitLockerStatus QueryStatus(const std::wstring& volumePath);

    // Suspend BitLocker protection before resizing or moving partition
    static bool SuspendProtection(const std::wstring& volumePath, uint32_t rebootCount = 1);

    // Resume BitLocker protection after operation completes
    static bool ResumeProtection(const std::wstring& volumePath);
};

// RAII Scope Guard to temporarily suspend BitLocker
class BitLockerSuspensionScope
{
public:
    explicit BitLockerSuspensionScope(const std::wstring& volumePath);
    ~BitLockerSuspensionScope();

    bool IsSuspended() const { return m_wasSuspended; }

private:
    std::wstring m_volumePath;
    bool m_wasSuspended;
};

} // namespace WParted

#endif // WPARTED_BITLOCKER_GUARD_H
