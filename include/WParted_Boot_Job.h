/* WParted — Native Windows Partition Editor (Forked from GParted)
 * Copyright (C) 2026 WParted Contributors
 *
 * Licensed under the GNU General Public License version 2 or later.
 */

#ifndef WPARTED_BOOT_JOB_H
#define WPARTED_BOOT_JOB_H

#include <cstdint>
#include <cstddef>

#pragma pack(push, 1)

namespace WParted
{

constexpr uint32_t BOOT_JOB_MAGIC = 0x54505742;  // 'BWPT' (Boot WParTed)
constexpr uint32_t BOOT_JOB_VERSION = 1;

enum BootOpType : uint32_t
{
    BOOT_OP_MOVE_SECTORS = 1,
    BOOT_OP_RESIZE_START = 2,
    BOOT_OP_RAW_COPY = 3
};

struct BootJobOperation
{
    uint32_t opType;             // BootOpType
    uint32_t diskIndex;          // Physical disk index (0, 1, ...)
    uint64_t sourceSector;       // Source start sector
    uint64_t targetSector;       // Target start sector
    uint64_t sectorCount;        // Number of sectors to move
    uint32_t sectorSize;         // Physical sector size (e.g. 512, 4096)
    uint64_t expectedCrc64;      // CRC64 of source data for validation
};

struct BootJobFileHeader
{
    uint32_t magic;              // BOOT_JOB_MAGIC
    uint32_t version;            // BOOT_JOB_VERSION
    uint32_t opCount;            // Number of operations in array
    uint32_t reserved;
    uint64_t headerCrc64;
    BootJobOperation operations[16];
};

// ECMA-182 CRC64 calculation
inline uint64_t CalculateCRC64(const void* data, size_t length, uint64_t initial = 0ULL)
{
    constexpr uint64_t POLY = 0x42F0E1EBA9EA3693ULL;
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    uint64_t crc = initial;

    for (size_t i = 0; i < length; ++i) {
        crc ^= static_cast<uint64_t>(bytes[i]) << 56;
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 0x8000000000000000ULL) {
                crc = (crc << 1) ^ POLY;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

} // namespace WParted

#pragma pack(pop)

#endif // WPARTED_BOOT_JOB_H
