/* WParted — Native Windows Partition Editor (Forked from GParted)
 * Copyright (C) 2026 WParted Contributors
 *
 * Licensed under the GNU General Public License version 2 or later.
 */

#ifndef WPARTED_APFS_STRUCTURES_H
#define WPARTED_APFS_STRUCTURES_H

#include <cstdint>
#include <cstddef>

#pragma pack(push, 1)

namespace WParted
{

// APFS Well-Known Constants
constexpr uint32_t APFS_NX_MAGIC = 0x4253584E;  // 'NXSB' in little-endian (0x4E585342 'BSXN')
constexpr uint32_t APFS_VOL_MAGIC = 0x53465041; // 'APFS' in little-endian

constexpr uint32_t APFS_FEATURE_ENCRYPTED = 0x00000001;
constexpr uint32_t APFS_INCOMPAT_CASE_INSENSITIVE = 0x00000001;
constexpr uint32_t APFS_INCOMPAT_DATALESS_SNAPS   = 0x00000002;
constexpr uint32_t APFS_INCOMPAT_ENC_ROLLED       = 0x00000004;

constexpr size_t APFS_MAX_HIST = 8;
constexpr size_t APFS_VOL_NAME_LEN = 128;

// Common physical object header
struct obj_phys_t
{
    uint64_t o_cksum;    // Fletcher 64 checksum
    uint64_t o_oid;      // Object ID
    uint64_t o_xid;      // Transaction ID
    uint32_t o_type;     // Object type
    uint32_t o_subtype;  // Object subtype
};

// APFS Container Superblock (nx_superblock_t)
struct nx_superblock_t
{
    obj_phys_t nx_o;
    uint32_t   nx_magic;                // 'NXSB' (0x4253584E)
    uint32_t   nx_block_size;           // Typically 4096
    uint64_t   nx_block_count;
    uint64_t   nx_features;
    uint64_t   nx_ro_compat_features;
    uint64_t   nx_incompat_features;
    uint8_t    nx_uuid[16];             // Container UUID
    uint64_t   nx_next_oid;
    uint64_t   nx_next_xid;
    uint32_t   nx_xp_desc_blocks;
    uint32_t   nx_xp_data_blocks;
    uint64_t   nx_xp_desc_base;
    uint64_t   nx_xp_data_base;
    uint32_t   nx_xp_desc_next;
    uint32_t   nx_xp_data_next;
    uint32_t   nx_xp_desc_index;
    uint32_t   nx_xp_desc_len;
    uint32_t   nx_xp_data_index;
    uint32_t   nx_xp_data_len;
    uint64_t   nx_spaceman_oid;
    uint64_t   nx_omap_oid;
    uint64_t   nx_reaper_oid;
    uint32_t   nx_test_type;
    uint32_t   nx_max_file_systems;
    uint64_t   nx_fs_oid[APFS_MAX_HIST];
};

// APFS Volume Superblock (apfs_superblock_t)
struct apfs_superblock_t
{
    obj_phys_t apfs_o;
    uint32_t   apfs_magic;               // 'APFS' (0x53465041)
    uint32_t   apfs_fs_index;
    uint64_t   apfs_features;
    uint64_t   apfs_ro_compat_features;
    uint64_t   apfs_incompat_features;
    uint64_t   apfs_unparsable_flags;
    uint64_t   apfs_omap_oid;
    uint64_t   apfs_root_tree_oid;
    uint64_t   apfs_extentref_tree_oid;
    uint64_t   apfs_snap_meta_tree_oid;
    uint64_t   apfs_revert_to_xid;
    uint64_t   apfs_revert_to_sblock_oid;
    uint64_t   apfs_next_obj_id;
    uint64_t   apfs_num_alloc_blocks;
    uint64_t   apfs_num_alloc_files;
    uint64_t   apfs_num_alloc_dirs;
    uint64_t   apfs_num_alloc_symlinks;
    uint64_t   apfs_num_alloc_other;
    uint64_t   apfs_num_snapshots;
    uint64_t   apfs_total_blocks_alloced;
    uint64_t   apfs_total_blocks_freed;
    uint8_t    apfs_vol_uuid[16];
    uint64_t   apfs_last_mod_time;
    uint64_t   apfs_fs_flags;
    uint8_t    apfs_formatted_by[32];
    uint64_t   apfs_formatted_by_time;
    uint8_t    apfs_vol_name[APFS_VOL_NAME_LEN];
};

// Calculate Fletcher 64 checksum used throughout APFS structures
inline uint64_t Fletcher64(const uint32_t* data, size_t numWords)
{
    uint64_t sum1 = 0;
    uint64_t sum2 = 0;
    constexpr uint64_t MOD = 0xFFFFFFFF;

    for (size_t i = 0; i < numWords; ++i) {
        sum1 = (sum1 + data[i]) % MOD;
        sum2 = (sum2 + sum1) % MOD;
    }

    uint64_t b = MOD - ((sum1 + sum2) % MOD);
    uint64_t a = MOD - ((sum1 + b) % MOD);
    return (b << 32) | a;
}

} // namespace WParted

#pragma pack(pop)

#endif // WPARTED_APFS_STRUCTURES_H
