/* WParted — Native Windows Partition Editor (Forked from GParted)
 * Copyright (C) 2026 WParted Contributors
 *
 * Licensed under the GNU General Public License version 2 or later.
 */

#include "FmifsWrapper.h"

#include <iostream>
#include <mutex>
#include <atomic>

namespace WParted
{

#if defined(_WIN32)
typedef BOOLEAN (WINAPI *PFMIFS_CALLBACK)(FmifsCallbackCommand Command, DWORD SubAction, PVOID ActionInfo);

typedef VOID (WINAPI *PFORMAT_EX)(
    PWCHAR DriveRoot,
    FmifsMediaType MediaType,
    PWCHAR FileSystem,
    PWCHAR Label,
    BOOLEAN QuickFormat,
    DWORD ClusterSize,
    PFMIFS_CALLBACK Callback
);

typedef VOID (WINAPI *PCHKDSK_EX)(
    PWCHAR DriveRoot,
    PWCHAR FileSystem,
    BOOLEAN FixErrors,
    BOOLEAN Verbose,
    BOOLEAN CheckOnlyIfDirty,
    BOOLEAN ScanBadSectors,
    PVOID Param6,
    PVOID Param7,
    PFMIFS_CALLBACK Callback
);

static std::mutex g_fmifsMutex;
static FmifsProgressCallback g_currentCallback = nullptr;
static std::atomic<bool> g_operationSuccess(false);

static BOOLEAN WINAPI InternalFmifsCallback(FmifsCallbackCommand command, DWORD subAction, PVOID actionInfo)
{
    (void)subAction;
    switch (command) {
        case FMIFS_PROGRESS: {
            if (actionInfo) {
                DWORD percent = *reinterpret_cast<DWORD*>(actionInfo);
                if (g_currentCallback) {
                    g_currentCallback(percent, "Processing...");
                }
            }
            break;
        }
        case FMIFS_STRUCTUREPROGRESS: {
            if (actionInfo && g_currentCallback) {
                DWORD percent = *reinterpret_cast<DWORD*>(actionInfo);
                g_currentCallback(percent, "Creating file system structures...");
            }
            break;
        }
        case FMIFS_FINISHED: {
            if (actionInfo) {
                BOOLEAN* pSuccess = reinterpret_cast<BOOLEAN*>(actionInfo);
                g_operationSuccess.store(*pSuccess != 0);
            }
            break;
        }
        case FMIFS_INSUFFICIENTRIGHTS: {
            if (g_currentCallback) {
                g_currentCallback(0, "Error: Insufficient privileges.");
            }
            g_operationSuccess.store(false);
            return FALSE;
        }
        case FMIFS_WRITEPROTECTED: {
            if (g_currentCallback) {
                g_currentCallback(0, "Error: Volume is write protected.");
            }
            g_operationSuccess.store(false);
            return FALSE;
        }
        case FMIFS_FSNOTSUPPORTED: {
            if (g_currentCallback) {
                g_currentCallback(0, "Error: File system not supported.");
            }
            g_operationSuccess.store(false);
            return FALSE;
        }
        case FMIFS_VOLUMEINUSE: {
            if (g_currentCallback) {
                g_currentCallback(0, "Warning: Volume is currently in use.");
            }
            break;
        }
        default:
            break;
    }
    return TRUE;
}
#endif

FmifsWrapper& FmifsWrapper::Instance()
{
    static FmifsWrapper instance;
    return instance;
}

FmifsWrapper::FmifsWrapper()
    : m_hFmifs(nullptr)
    , m_pfnFormatEx(nullptr)
    , m_pfnChkdskEx(nullptr)
{
#if defined(_WIN32)
    m_hFmifs = LoadLibraryW(L"fmifs.dll");
    if (m_hFmifs) {
        m_pfnFormatEx = reinterpret_cast<void*>(GetProcAddress(m_hFmifs, "FormatEx"));
        m_pfnChkdskEx = reinterpret_cast<void*>(GetProcAddress(m_hFmifs, "ChkdskEx"));
    }
#endif
}

FmifsWrapper::~FmifsWrapper()
{
#if defined(_WIN32)
    if (m_hFmifs) {
        FreeLibrary(m_hFmifs);
        m_hFmifs = nullptr;
    }
#endif
}

bool FmifsWrapper::FormatVolume(const std::wstring& driveRoot,
                                const std::wstring& fileSystem,
                                const std::wstring& label,
                                bool quickFormat,
                                uint32_t clusterSize,
                                FmifsProgressCallback callback)
{
#if defined(_WIN32)
    if (!m_pfnFormatEx) {
        return false;
    }

    std::lock_guard<std::mutex> lock(g_fmifsMutex);
    g_currentCallback = callback;
    g_operationSuccess.store(false);

    std::vector<wchar_t> driveBuf(driveRoot.begin(), driveRoot.end());
    driveBuf.push_back(L'\0');

    std::vector<wchar_t> fsBuf(fileSystem.begin(), fileSystem.end());
    fsBuf.push_back(L'\0');

    std::vector<wchar_t> labelBuf(label.begin(), label.end());
    labelBuf.push_back(L'\0');

    PFORMAT_EX pFormat = reinterpret_cast<PFORMAT_EX>(m_pfnFormatEx);
    pFormat(
        driveBuf.data(),
        FMIFS_HARDDISK,
        fsBuf.data(),
        labelBuf.data(),
        quickFormat ? TRUE : FALSE,
        clusterSize,
        InternalFmifsCallback
    );

    g_currentCallback = nullptr;
    return g_operationSuccess.load();
#else
    (void)driveRoot; (void)fileSystem; (void)label; (void)quickFormat; (void)clusterSize; (void)callback;
    return true;
#endif
}

bool FmifsWrapper::CheckVolume(const std::wstring& driveRoot,
                               const std::wstring& fileSystem,
                               bool fixErrors,
                               bool verbose,
                               bool scanBadSectors,
                               FmifsProgressCallback callback)
{
#if defined(_WIN32)
    if (!m_pfnChkdskEx) {
        return false;
    }

    std::lock_guard<std::mutex> lock(g_fmifsMutex);
    g_currentCallback = callback;
    g_operationSuccess.store(false);

    std::vector<wchar_t> driveBuf(driveRoot.begin(), driveRoot.end());
    driveBuf.push_back(L'\0');

    std::vector<wchar_t> fsBuf(fileSystem.begin(), fileSystem.end());
    fsBuf.push_back(L'\0');

    PCHKDSK_EX pChkdsk = reinterpret_cast<PCHKDSK_EX>(m_pfnChkdskEx);
    pChkdsk(
        driveBuf.data(),
        fsBuf.data(),
        fixErrors ? TRUE : FALSE,
        verbose ? TRUE : FALSE,
        FALSE,
        scanBadSectors ? TRUE : FALSE,
        NULL,
        NULL,
        InternalFmifsCallback
    );

    g_currentCallback = nullptr;
    return g_operationSuccess.load();
#else
    (void)driveRoot; (void)fileSystem; (void)fixErrors; (void)verbose; (void)scanBadSectors; (void)callback;
    return true;
#endif
}

} // namespace WParted
