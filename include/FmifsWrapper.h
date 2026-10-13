/* WParted — Native Windows Partition Editor (Forked from GParted)
 * Copyright (C) 2026 WParted Contributors
 *
 * Licensed under the GNU General Public License version 2 or later.
 */

#ifndef WPARTED_FMIFS_WRAPPER_H
#define WPARTED_FMIFS_WRAPPER_H

#include <string>
#include <functional>
#include <cstdint>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
typedef int BOOLEAN;
typedef unsigned long DWORD;
typedef void* PVOID;
typedef wchar_t* PWCHAR;
#endif

namespace WParted
{

enum FmifsMediaType
{
    FMIFS_UNKNOWN_MEDIA = 0,
    FMIFS_HARDDISK = 12
};

enum FmifsCallbackCommand
{
    FMIFS_PROGRESS = 0,
    FMIFS_DONEWITHSTRUCTURE = 1,
    FMIFS_INSUFFICIENTRIGHTS = 3,
    FMIFS_WRITEPROTECTED = 4,
    FMIFS_FSNOTSUPPORTED = 9,
    FMIFS_VOLUMEINUSE = 10,
    FMIFS_STRUCTUREPROGRESS = 15,
    FMIFS_VOLUMEISDIRTY = 17,
    FMIFS_STATUS = 18,
    FMIFS_FINISHED = 20
};

using FmifsProgressCallback = std::function<void(uint32_t percent, const std::string& message)>;

class FmifsWrapper
{
public:
    static FmifsWrapper& Instance();

    // Format volume (e.g. L"D:\\", "NTFS", "MyVolume", true, 4096)
    bool FormatVolume(const std::wstring& driveRoot,
                      const std::wstring& fileSystem,
                      const std::wstring& label,
                      bool quickFormat,
                      uint32_t clusterSize,
                      FmifsProgressCallback callback = nullptr);

    // Run Chkdsk on volume
    bool CheckVolume(const std::wstring& driveRoot,
                     const std::wstring& fileSystem,
                     bool fixErrors,
                     bool verbose,
                     bool scanBadSectors,
                     FmifsProgressCallback callback = nullptr);

    bool IsAvailable() const { return m_hFmifs != nullptr; }

private:
    FmifsWrapper();
    ~FmifsWrapper();

    FmifsWrapper(const FmifsWrapper&) = delete;
    FmifsWrapper& operator=(const FmifsWrapper&) = delete;

    HMODULE m_hFmifs;
    void* m_pfnFormatEx;
    void* m_pfnChkdskEx;
};

} // namespace WParted

#endif // WPARTED_FMIFS_WRAPPER_H
