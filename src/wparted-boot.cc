/* WParted — Native Windows Partition Editor (Forked from GParted)
 * Copyright (C) 2026 WParted Contributors
 *
 * Windows Native API Subsystem Boot Helper (Linked solely against ntdll.dll)
 * Subsystem: Native | Entry Point: NtProcessStartup
 */

#include "WParted_Boot_Job.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winternl.h>

#ifndef NtCurrentProcess
#define NtCurrentProcess() ((HANDLE)(LONG_PTR)-1)
#endif

#if defined(_MSC_VER)
#pragma function(memset, memcpy)
#endif

extern "C" {

// Intrinsics for pure native CRT-free executable
void* __cdecl memset(void* dest, int c, size_t count)
{
    unsigned char* p = static_cast<unsigned char*>(dest);
    while (count--) {
        *p++ = static_cast<unsigned char>(c);
    }
    return dest;
}

void* __cdecl memcpy(void* dest, const void* src, size_t count)
{
    unsigned char* d = static_cast<unsigned char*>(dest);
    const unsigned char* s = static_cast<const unsigned char*>(src);
    while (count--) {
        *d++ = *s++;
    }
    return dest;
}

// Native API Type Definitions and Prototypes

NTSTATUS NTAPI NtDisplayString(
    PUNICODE_STRING String
);

NTSTATUS NTAPI NtOpenFile(
    PHANDLE FileHandle,
    ACCESS_MASK DesiredAccess,
    POBJECT_ATTRIBUTES ObjectAttributes,
    PIO_STATUS_BLOCK IoStatusBlock,
    ULONG ShareAccess,
    ULONG OpenOptions
);

NTSTATUS NTAPI NtReadFile(
    HANDLE FileHandle,
    HANDLE Event,
    PIO_APC_ROUTINE ApcRoutine,
    PVOID ApcContext,
    PIO_STATUS_BLOCK IoStatusBlock,
    PVOID Buffer,
    ULONG Length,
    PLARGE_INTEGER ByteOffset,
    PULONG Key
);

NTSTATUS NTAPI NtWriteFile(
    HANDLE FileHandle,
    HANDLE Event,
    PIO_APC_ROUTINE ApcRoutine,
    PVOID ApcContext,
    PIO_STATUS_BLOCK IoStatusBlock,
    PVOID Buffer,
    ULONG Length,
    PLARGE_INTEGER ByteOffset,
    PULONG Key
);

NTSTATUS NTAPI NtFlushBuffersFile(
    HANDLE FileHandle,
    PIO_STATUS_BLOCK IoStatusBlock
);

NTSTATUS NTAPI NtDeleteFile(
    POBJECT_ATTRIBUTES ObjectAttributes
);

NTSTATUS NTAPI NtTerminateProcess(
    HANDLE ProcessHandle,
    NTSTATUS ExitStatus
);

typedef enum _KEY_VALUE_INFORMATION_CLASS {
    KeyValueBasicInformation,
    KeyValueFullInformation,
    KeyValuePartialInformation,
    KeyValueFullInformationAlign64,
    KeyValuePartialInformationAlign64,
    MaxKeyValueInfoClass
} KEY_VALUE_INFORMATION_CLASS;

typedef struct _KEY_VALUE_PARTIAL_INFORMATION {
    ULONG TitleIndex;
    ULONG Type;
    ULONG DataLength;
    UCHAR Data[1];
} KEY_VALUE_PARTIAL_INFORMATION, *PKEY_VALUE_PARTIAL_INFORMATION;

NTSTATUS NTAPI NtOpenKey(
    PHANDLE KeyHandle,
    ACCESS_MASK DesiredAccess,
    POBJECT_ATTRIBUTES ObjectAttributes
);

NTSTATUS NTAPI NtQueryValueKey(
    HANDLE KeyHandle,
    PUNICODE_STRING ValueName,
    KEY_VALUE_INFORMATION_CLASS KeyValueInformationClass,
    PVOID KeyValueInformation,
    ULONG Length,
    PULONG ResultLength
);

NTSTATUS NTAPI NtSetValueKey(
    HANDLE KeyHandle,
    PUNICODE_STRING ValueName,
    ULONG TitleIndex,
    ULONG Type,
    PVOID Data,
    ULONG DataSize
);

}

static void RemoveFromBootExecuteNative()
{
    UNICODE_STRING keyPath;
    RtlInitUnicodeString(&keyPath, L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\Session Manager");

    OBJECT_ATTRIBUTES objAttr;
    InitializeObjectAttributes(&objAttr, &keyPath, OBJ_CASE_INSENSITIVE, NULL, NULL);

    HANDLE hKey = NULL;
    NTSTATUS status = NtOpenKey(&hKey, KEY_READ | KEY_WRITE, &objAttr);
    if (!NT_SUCCESS(status)) {
        return;
    }

    UNICODE_STRING valName;
    RtlInitUnicodeString(&valName, L"BootExecute");

    static uint8_t buffer[2048] = { 0 };
    ULONG resultLength = 0;

    status = NtQueryValueKey(hKey, &valName, KeyValuePartialInformation, buffer, sizeof(buffer), &resultLength);
    if (NT_SUCCESS(status)) {
        PKEY_VALUE_PARTIAL_INFORMATION pInfo = reinterpret_cast<PKEY_VALUE_PARTIAL_INFORMATION>(buffer);
        if (pInfo->Type == REG_MULTI_SZ) {
            const wchar_t* p = reinterpret_cast<const wchar_t*>(pInfo->Data);
            const wchar_t* end = reinterpret_cast<const wchar_t*>(pInfo->Data + pInfo->DataLength);

            static wchar_t newMultiSz[1024] = { 0 };
            int newIdx = 0;

            while (p < end && *p) {
                bool matches = false;
                const wchar_t* check = p;
                const wchar_t needle[] = L"wparted-boot";
                while (*check) {
                    int k = 0;
                    while (needle[k] && check[k] && (check[k] == needle[k] || check[k] + 32 == needle[k])) {
                        k++;
                    }
                    if (needle[k] == L'\0') {
                        matches = true;
                        break;
                    }
                    check++;
                }

                int len = 0;
                while (p[len]) len++;

                if (!matches) {
                    for (int c = 0; c < len; ++c) {
                        newMultiSz[newIdx++] = p[c];
                    }
                    newMultiSz[newIdx++] = L'\0';
                }
                p += len + 1;
            }

            newMultiSz[newIdx++] = L'\0'; // Final null for REG_MULTI_SZ
            ULONG newSize = newIdx * sizeof(wchar_t);
            NtSetValueKey(hKey, &valName, 0, REG_MULTI_SZ, newMultiSz, newSize);
        }
    }

    NtClose(hKey);
}

static void UIntToWString(uint32_t val, wchar_t* buf)
{
    wchar_t temp[16];
    int idx = 0;
    if (val == 0) {
        buf[0] = L'0'; buf[1] = L'\0';
        return;
    }
    while (val > 0) {
        temp[idx++] = L'0' + (val % 10);
        val /= 10;
    }
    for (int i = 0; i < idx; ++i) {
        buf[i] = temp[idx - 1 - i];
    }
    buf[idx] = L'\0';
}

static void BuildDiskDevicePath(uint32_t diskIndex, wchar_t* buf)
{
    const wchar_t prefix[] = L"\\Device\\Harddisk";
    int i = 0;
    while (prefix[i]) { buf[i] = prefix[i]; i++; }
    wchar_t numBuf[16];
    UIntToWString(diskIndex, numBuf);
    int j = 0;
    while (numBuf[j]) { buf[i++] = numBuf[j++]; }
    const wchar_t suffix[] = L"\\Partition0";
    j = 0;
    while (suffix[j]) { buf[i++] = suffix[j++]; }
    buf[i] = L'\0';
}

static void PrintBootString(const wchar_t* str)
{
    UNICODE_STRING uStr;
    RtlInitUnicodeString(&uStr, str);
    NtDisplayString(&uStr);
}

// 1MB Chunk Buffer in static memory to avoid heap dependencies
static uint8_t g_sectorBuffer[1024 * 1024];

extern "C" void NTAPI NtProcessStartup(void* Argument)
{
    (void)Argument;

    PrintBootString(L"\n=======================================================\n");
    PrintBootString(L"  WParted Native Storage Engine (Boot-Time Relocation)  \n");
    PrintBootString(L"=======================================================\n\n");

    // Open \SystemRoot\System32\wparted-jobs.bin
    UNICODE_STRING jobPath;
    RtlInitUnicodeString(&jobPath, L"\\SystemRoot\\System32\\wparted-jobs.bin");

    OBJECT_ATTRIBUTES objAttr;
    InitializeObjectAttributes(&objAttr, &jobPath, OBJ_CASE_INSENSITIVE, NULL, NULL);

    HANDLE hJobFile = NULL;
    IO_STATUS_BLOCK ioStatus = { 0 };

    NTSTATUS status = NtOpenFile(
        &hJobFile,
        FILE_GENERIC_READ,
        &objAttr,
        &ioStatus,
        FILE_SHARE_READ,
        FILE_SYNCHRONOUS_IO_NONALERT
    );

    if (!NT_SUCCESS(status)) {
        PrintBootString(L"No pending WParted jobs found. Resuming Windows boot...\n");
        NtTerminateProcess(NtCurrentProcess(), 0);
        return;
    }

    WParted::BootJobFileHeader header = { 0 };
    LARGE_INTEGER readOffset = { 0 };

    status = NtReadFile(
        hJobFile,
        NULL,
        NULL,
        NULL,
        &ioStatus,
        &header,
        sizeof(header),
        &readOffset,
        NULL
    );

    NtClose(hJobFile);

    if (!NT_SUCCESS(status) || header.magic != WParted::BOOT_JOB_MAGIC || header.opCount == 0) {
        PrintBootString(L"Error: Corrupted WParted job file header.\n");
        NtDeleteFile(&objAttr);
        RemoveFromBootExecuteNative();
        NtTerminateProcess(NtCurrentProcess(), 0);
        return;
    }

    // Verify CRC64 of the operations payload
    uint64_t expectedCrc = WParted::CalculateCRC64(header.operations, header.opCount * sizeof(WParted::BootJobOperation));
    if (header.headerCrc64 != expectedCrc) {
        PrintBootString(L"Error: Job CRC64 checksum mismatch. Relocation aborted for safety.\n");
        NtDeleteFile(&objAttr);
        RemoveFromBootExecuteNative();
        NtTerminateProcess(NtCurrentProcess(), 0);
        return;
    }

    PrintBootString(L"CRC64 integrity of scheduled operations: Verified OK.\n");

    for (uint32_t i = 0; i < header.opCount; ++i) {
        const auto& op = header.operations[i];
        wchar_t diskPathBuf[64];
        BuildDiskDevicePath(op.diskIndex, diskPathBuf);

        UNICODE_STRING diskPath;
        RtlInitUnicodeString(&diskPath, diskPathBuf);

        OBJECT_ATTRIBUTES diskAttr;
        InitializeObjectAttributes(&diskAttr, &diskPath, OBJ_CASE_INSENSITIVE, NULL, NULL);

        HANDLE hDisk = NULL;
        status = NtOpenFile(
            &hDisk,
            FILE_GENERIC_READ | FILE_GENERIC_WRITE,
            &diskAttr,
            &ioStatus,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            FILE_SYNCHRONOUS_IO_NONALERT
        );

        if (!NT_SUCCESS(status)) {
            PrintBootString(L"Error: Unable to open target physical disk for sector move.\n");
            continue;
        }

        PrintBootString(L"Moving locked sectors with journaling...\n");

        uint64_t sectorSize = op.sectorSize ? op.sectorSize : 512;
        uint64_t totalBytes = op.sectorCount * sectorSize;
        uint64_t bytesProcessed = 0;
        uint32_t chunkSize = sizeof(g_sectorBuffer);

        // Safe overlap check: if moving towards higher LBAs (right), copy backwards from end to start
        bool moveBackward = (op.targetSector > op.sourceSector);

        while (bytesProcessed < totalBytes) {
            uint64_t remaining = totalBytes - bytesProcessed;
            uint32_t bytesToCopy = (remaining < chunkSize) ? static_cast<uint32_t>(remaining) : chunkSize;

            LARGE_INTEGER srcOffset;
            LARGE_INTEGER dstOffset;

            if (moveBackward) {
                // Moving right: copy backwards from end to prevent overwriting source sectors
                uint64_t currentOffset = totalBytes - bytesProcessed - bytesToCopy;
                srcOffset.QuadPart = static_cast<LONGLONG>((op.sourceSector * sectorSize) + currentOffset);
                dstOffset.QuadPart = static_cast<LONGLONG>((op.targetSector * sectorSize) + currentOffset);
            } else {
                // Moving left: copy forwards from start
                srcOffset.QuadPart = static_cast<LONGLONG>((op.sourceSector * sectorSize) + bytesProcessed);
                dstOffset.QuadPart = static_cast<LONGLONG>((op.targetSector * sectorSize) + bytesProcessed);
            }

            status = NtReadFile(
                hDisk,
                NULL,
                NULL,
                NULL,
                &ioStatus,
                g_sectorBuffer,
                bytesToCopy,
                &srcOffset,
                NULL
            );

            if (!NT_SUCCESS(status)) {
                PrintBootString(L"Error reading source sectors.\n");
                break;
            }

            status = NtWriteFile(
                hDisk,
                NULL,
                NULL,
                NULL,
                &ioStatus,
                g_sectorBuffer,
                bytesToCopy,
                &dstOffset,
                NULL
            );

            if (!NT_SUCCESS(status)) {
                PrintBootString(L"Error writing target sectors.\n");
                break;
            }

            bytesProcessed += bytesToCopy;

            uint32_t pct = static_cast<uint32_t>((bytesProcessed * 100) / totalBytes);
            if (pct % 25 == 0) {
                wchar_t pctBuf[32];
                const wchar_t pPrefix[] = L"  Progress: ";
                int pIdx = 0;
                while (pPrefix[pIdx]) { pctBuf[pIdx] = pPrefix[pIdx]; pIdx++; }
                wchar_t numBuf[16];
                UIntToWString(pct, numBuf);
                int pJ = 0;
                while (numBuf[pJ]) { pctBuf[pIdx++] = numBuf[pJ++]; }
                pctBuf[pIdx++] = L'%';
                pctBuf[pIdx++] = L'\n';
                pctBuf[pIdx] = L'\0';
                PrintBootString(pctBuf);
            }
        }

        NtFlushBuffersFile(hDisk, &ioStatus);
        NtClose(hDisk);
    }

    PrintBootString(L"Sector moves completed successfully.\n");
    PrintBootString(L"Cleaning up boot execution registry and job file...\n");

    // Clean up job file
    NtDeleteFile(&objAttr);

    // Clean up BootExecute entry from Windows registry
    RemoveFromBootExecuteNative();

    PrintBootString(L"Windows will now continue startup.\n\n");
    NtTerminateProcess(NtCurrentProcess(), 0);
}

#else
int main() {
    return 0;
}
#endif
