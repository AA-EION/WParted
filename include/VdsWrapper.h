/* WParted — Native Windows Partition Editor (Forked from GParted)
 * Copyright (C) 2026 WParted Contributors
 *
 * Licensed under the GNU General Public License version 2 or later.
 */

#ifndef WPARTED_VDS_WRAPPER_H
#define WPARTED_VDS_WRAPPER_H

#include <string>
#include <cstdint>

namespace WParted
{

struct VolumeResizeCapabilities
{
    uint64_t currentSizeBytes = 0;
    uint64_t minSupportedSizeBytes = 0;
    uint64_t maxSupportedSizeBytes = 0;
    bool canShrinkOnline = false;
    bool canExtendOnline = false;
};

class VdsWrapper
{
public:
    static VdsWrapper& Instance();

    // Query online resize capabilities (min/max bounds) for live NTFS/ReFS volume
    VolumeResizeCapabilities QueryResizeCapabilities(const std::wstring& volumePath);

    // Live online shrink of volume (e.g. C:\) while Windows is running
    bool ShrinkVolumeOnline(const std::wstring& volumePath, uint64_t bytesToShrink);

    // Live online extend of volume while Windows is running
    bool ExtendVolumeOnline(const std::wstring& volumePath, uint64_t bytesToAdd);

    // Initialize VDS COM service
    bool InitializeVds();

    // Cleanup VDS COM service
    void UninitializeVds();

private:
    VdsWrapper();
    ~VdsWrapper();

    VdsWrapper(const VdsWrapper&) = delete;
    VdsWrapper& operator=(const VdsWrapper&) = delete;

    bool m_vdsInitialized;
};

} // namespace WParted

#endif // WPARTED_VDS_WRAPPER_H
