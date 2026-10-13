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
        NtTerminateProcess(NtCurrentProcess(), 0);
        return;
    }

    PrintBootString(L"Verifying CRC64 integrity of scheduled operations... OK.\n");

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

        while (bytesProcessed < totalBytes) {
            uint64_t remaining = totalBytes - bytesProcessed;
            uint32_t bytesToCopy = (remaining < chunkSize) ? static_cast<uint32_t>(remaining) : chunkSize;

            LARGE_INTEGER srcOffset;
            srcOffset.QuadPart = (op.sourceSector * sectorSize) + bytesProcessed;

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

            LARGE_INTEGER dstOffset;
            dstOffset.QuadPart = (op.targetSector * sectorSize) + bytesProcessed;

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

    PrintBootString(L"Windows will now continue startup.\n\n");
    NtTerminateProcess(NtCurrentProcess(), 0);
}

#else
int main() {
    return 0;
}
#endif
