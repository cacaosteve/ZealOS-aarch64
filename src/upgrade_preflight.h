/* Read-only recognition of the two UTM installer layouts. */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "block_partition.h"
#include "disk_layout.h"

struct zeal_upgrade_target {
    uint64_t esp_first, esp_sectors;
    uint64_t data_first, data_sectors;
    uint64_t bank_a, bank_b;
    int legacy;
};

/* The legacy and expanded images leave the region between the primary GPT
 * and EFI partition unused. This only tests whether it is all zero; it does
 * not reserve it, and zero bytes alone cannot establish ownership. */
static int zeal_upgrade_gap_empty(zeal_partition_read_fn read, void *context) {
    uint8_t sector[512];
    if (!read) return 0;
    for (uint64_t lba = 34; lba < 2048; lba++) {
        if (!read(context, lba, sector)) return 0;
        for (unsigned i = 0; i < 512; i++) if (sector[i]) return 0;
    }
    return 1;
}

static int zeal_upgrade_guid_eq(const uint8_t *a, const uint8_t *b) {
    for (unsigned i = 0; i < 16; i++) if (a[i] != b[i]) return 0;
    return 1;
}

static int zeal_upgrade_bank_valid(zeal_partition_read_fn read, void *context,
                                   uint64_t lba) {
    uint8_t sector[512];
    static const uint8_t magic[7] = {'Z','C','S','R','C','0','0'};
    if (!read(context, lba, sector)) return 0;
    for (unsigned i = 0; i < 7; i++) if (sector[i] != magic[i]) return 0;
    if (zeal_partition_u32(sector + 12) != ZEAL_SRC_SECTS) return 0;
    if (sector[7] == '1' && zeal_partition_u32(sector + 8) == 1) return 1;
    return sector[7] == '2' && zeal_partition_u32(sector + 8) == 2 &&
           zeal_partition_u32(sector + 16) != 0 &&
           zeal_partition_u32(sector + 20) == zeal_partition_crc(sector, 20);
}

/* Check both GPT copies before an upgrade. A valid primary alone is not
 * sufficient evidence that the disk's partition metadata is consistent. */
static int zeal_upgrade_gpt_mirrors_valid(zeal_partition_read_fn read, void *context,
                                          uint64_t capacity) {
    uint8_t primary[512], backup[512], a[512], b[512];
    if (capacity < 68 || !read(context, 1, primary) ||
        !read(context, capacity - 1, backup)) return 0;
    for (unsigned i = 0; i < 8; i++)
        if (primary[i] != backup[i]) return 0;
    uint32_t size = zeal_partition_u32(backup + 12);
    uint32_t saved_crc = zeal_partition_u32(backup + 16);
    if (size < 92 || size > 512 ||
        zeal_partition_u64(primary + 32) != capacity - 1 ||
        zeal_partition_u64(backup + 24) != capacity - 1 ||
        zeal_partition_u64(backup + 32) != 1 ||
        zeal_partition_u64(primary + 72) != 2 ||
        zeal_partition_u32(primary + 80) != 128 ||
        zeal_partition_u32(primary + 84) != 128 ||
        zeal_partition_u64(backup + 72) != capacity - 33) return 0;
    for (unsigned i = 8; i < 16; i++) if (primary[i] != backup[i]) return 0;
    for (unsigned i = 40; i < 72; i++) if (primary[i] != backup[i]) return 0;
    for (unsigned i = 80; i < 92; i++) if (primary[i] != backup[i]) return 0;
    for (unsigned i = 16; i < 20; i++) backup[i] = 0;
    if (zeal_partition_crc(backup, size) != saved_crc) return 0;
    for (unsigned sector = 0; sector < 32; sector++) {
        if (!read(context, 2 + sector, a) ||
            !read(context, capacity - 33 + sector, b)) return 0;
        for (unsigned i = 0; i < 512; i++) if (a[i] != b[i]) return 0;
    }
    return 1;
}

/* Returns 1 only for a recognized, structurally valid existing installation.
 * This function performs no writes and never treats an unmarked disk as safe. */
static int zeal_upgrade_preflight(zeal_partition_read_fn read, void *context,
                                  uint64_t capacity, struct zeal_upgrade_target *out) {
    static const uint8_t efi_guid[16] = {
        0x28,0x73,0x2a,0xc1,0x1f,0xf8,0xd2,0x11,
        0xba,0x4b,0x00,0xa0,0xc9,0x3e,0xc9,0x3b};
    static const uint8_t linux_guid[16] = {
        0xaf,0x3d,0xc6,0x0f,0x83,0x84,0x72,0x47,
        0x8e,0x79,0x3d,0x69,0xd8,0x47,0x7d,0xe4};
    struct zeal_partition parts[ZEAL_PARTITION_MAX];
    struct zeal_upgrade_target target = {0};
    size_t count = 0, efi_count = 0, data_count = 0;
    uint8_t boot[512];
    if (!read || !out || capacity < ZEAL_DISK_SECTS ||
        zeal_partition_scan(read, context, capacity, parts,
                            ZEAL_PARTITION_MAX, &count) != ZEAL_PARTITION_OK ||
        !zeal_upgrade_gpt_mirrors_valid(read, context, capacity))
        return 0;
    for (size_t i = 0; i < count; i++) {
        if (parts[i].scheme != ZEAL_PARTITION_GPT) return 0;
        if (zeal_upgrade_guid_eq(parts[i].type_guid, efi_guid)) {
            efi_count++;
            target.esp_first = parts[i].first_lba;
            target.esp_sectors = parts[i].sector_count;
        }
        if (zeal_upgrade_guid_eq(parts[i].type_guid, ZEAL_PARTITION_GPT_ZEAL_DATA_GUID)) {
            data_count++;
            target.data_first = parts[i].first_lba;
            target.data_sectors = parts[i].sector_count;
        }
    }
    if (efi_count != 1 || target.esp_first != 2048 ||
        target.esp_sectors != ZEAL_RS_LBA_BASE - 2048 ||
        !read(context, target.esp_first, boot) || boot[510] != 0x55 || boot[511] != 0xaa ||
        !((boot[0] == 0xeb && boot[2] == 0x90) || boot[0] == 0xe9) ||
        (boot[11] | ((unsigned)boot[12] << 8)) != 512 || boot[13] != 1 ||
        (boot[14] | ((unsigned)boot[15] << 8)) != 32 || boot[16] != 2 ||
        boot[17] || boot[18] || boot[19] || boot[20] || boot[22] || boot[23] ||
        zeal_partition_u32(boot + 32) != target.esp_sectors ||
        !zeal_partition_u32(boot + 36) ||
        (uint64_t)32 + 2ull * zeal_partition_u32(boot + 36) >= target.esp_sectors ||
        zeal_partition_u32(boot + 44) != 2)
        return 0;
    if (data_count == 1) {
        if (count != 2 || target.data_first != ZEAL_RS_LBA_BASE ||
            target.data_sectors <= ZEAL_SRC_SECTS + ZEAL_SRC_SHADOW_SECTS ||
            target.data_first + target.data_sectors != capacity - 33) return 0;
        target.bank_b = capacity - 33 - ZEAL_SRC_SHADOW_SECTS;
        target.bank_a = target.bank_b - ZEAL_SRC_SECTS;
        if (!read(context, target.data_first, boot) || boot[3] != 0x88 ||
            zeal_partition_u64(boot + 16) != target.bank_a - target.data_first)
            return 0;
    } else if (data_count == 0 && capacity == ZEAL_DISK_SECTS && count == 3) {
        int have_a = 0, have_b = 0;
        for (size_t i = 0; i < count; i++) {
            if (!zeal_upgrade_guid_eq(parts[i].type_guid, linux_guid)) continue;
            if (parts[i].first_lba == ZEAL_SRC_LBA_BASE && parts[i].sector_count == ZEAL_SRC_SECTS) have_a++;
            if (parts[i].first_lba == ZEAL_SRC_SHADOW_LBA && parts[i].sector_count == ZEAL_SRC_SHADOW_SECTS) have_b++;
        }
        if (have_a != 1 || have_b != 1) return 0;
        target.legacy = 1;
        target.bank_a = ZEAL_SRC_LBA_BASE;
        target.bank_b = ZEAL_SRC_SHADOW_LBA;
    } else return 0;
    if (!zeal_upgrade_bank_valid(read, context, target.bank_a) &&
        !zeal_upgrade_bank_valid(read, context, target.bank_b)) return 0;
    *out = target;
    return 1;
}
