/* Read-only FAT32 allocation scan used to prepare an EFI update.
 * This does not reserve clusters or write to the filesystem. */
#pragma once
#include <stdint.h>
#include "block_partition.h"

struct efi_fat32_space {
    uint64_t fat_first, data_first;
    uint32_t fat_sectors, clusters, free_clusters, largest_run;
    uint8_t sectors_per_cluster;
};
static uint16_t efi_fat32_u16(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

/* Only mirrored FAT32 volumes are accepted. Check every valid FAT entry in
 * both copies before returning space information: picking a free cluster from
 * a stale mirror could overwrite a file. The callback is read-only. */
static int efi_fat32_scan_space(zeal_partition_read_fn read, void *context,
                                uint64_t first, uint64_t sectors,
                                struct efi_fat32_space *out) {
    uint8_t boot[512], a[512], b[512];
    struct efi_fat32_space info = {0};
    if (!read || !out || !sectors || first > UINT64_MAX - sectors ||
        !read(context, first, boot) || boot[510] != 0x55 || boot[511] != 0xaa ||
        efi_fat32_u16(boot + 11) != 512 || boot[16] != 2 ||
        efi_fat32_u16(boot + 40) != 0 || efi_fat32_u16(boot + 42) != 0 ||
        efi_fat32_u16(boot + 17) || efi_fat32_u16(boot + 19) ||
        efi_fat32_u16(boot + 22)) return 0;
    uint32_t spc = boot[13], reserved = efi_fat32_u16(boot + 14);
    uint32_t total = zeal_partition_u32(boot + 32), fat = zeal_partition_u32(boot + 36);
    uint64_t overhead = (uint64_t)reserved + 2ull * fat;
    if (!spc || spc > 128 || (spc & (spc - 1)) || !reserved || !fat ||
        total > sectors || overhead >= total) return 0;
    uint64_t clusters = (total - overhead) / spc;
    if (clusters < 65525 || clusters + 2 > (uint64_t)fat * 128 ||
        clusters + 1 >= 0x0ffffff0u || zeal_partition_u32(boot + 44) < 2 ||
        zeal_partition_u32(boot + 44) > clusters + 1) return 0;
    info.fat_first = first + reserved; info.fat_sectors = fat;
    info.data_first = first + overhead; info.sectors_per_cluster = (uint8_t)spc;
    info.clusters = (uint32_t)clusters;
    uint32_t run = 0;
    for (uint32_t sector = 0; sector < fat; sector++) {
        if (!read(context, info.fat_first + sector, a) ||
            !read(context, info.fat_first + fat + sector, b)) return 0;
        for (unsigned i = 0; i < 128; i++) {
            uint64_t cluster = (uint64_t)sector * 128 + i;
            if (cluster > clusters + 1) break;
            uint32_t av = zeal_partition_u32(a + 4*i) & 0x0fffffffu;
            uint32_t bv = zeal_partition_u32(b + 4*i) & 0x0fffffffu;
            if (av != bv) return 0;
            if (cluster < 2) continue;
            if (!av) {
                info.free_clusters++; run++;
                if (run > info.largest_run) info.largest_run = run;
            } else run = 0;
        }
    }
    *out = info;
    return 1;
}

typedef int (*efi_fat32_write_fn)(void *, uint64_t, const uint8_t[512]);
typedef int (*efi_fat32_flush_fn)(void *);

/* Create a new 8.3 file in the root directory on a contiguous chain. The
 * caller must provide exclusive access to the volume and durable flushes.
 * Failure after metadata writes can leak clusters or require FAT repair:
 * this primitive is NOT interruption-recoverable and is not an updater. */
static int efi_fat32_create_root_ex(zeal_partition_read_fn read, efi_fat32_write_fn write,
                                 efi_fat32_write_fn metadata_write, efi_fat32_flush_fn flush, void *ctx,
                                 uint64_t first, uint64_t sectors,
                                 const uint8_t name[11], const uint8_t *data, uint32_t bytes) {
    struct efi_fat32_space s;
    uint8_t boot[512], a[512], b[512], verify[512];
    uint64_t dir_sector = 0; unsigned dir_offset = 0;
    uint32_t run = 0, start = 0, root;
    if (!read || !write || !metadata_write || !flush || !name || !data || !bytes) return 0;
    for (unsigned i = 0; i < 11; i++) {
        uint8_t c = name[i];
        if (c != ' ' && !(c >= 'A' && c <= 'Z') && !(c >= '0' && c <= '9') && c != '_') return 0;
    }
    if (name[0] == ' ' || !efi_fat32_scan_space(read, ctx, first, sectors, &s) ||
        !read(ctx, first, boot)) return 0;
    uint32_t fsinfo = efi_fat32_u16(boot + 48), backup = efi_fat32_u16(boot + 50);
    uint32_t reserved = efi_fat32_u16(boot + 14);
    if (!fsinfo || fsinfo >= reserved || !backup || backup + fsinfo >= reserved) return 0;
    int backup_fsinfo = 1;
    for (unsigned copy = 0; copy < 2; copy++) {
        if (!read(ctx, first + fsinfo + (copy ? backup : 0), b)) return 0;
        int empty = 1;
        for (unsigned i = 0; i < 512; i++) if (b[i]) { empty = 0; break; }
        if (copy && empty) { backup_fsinfo = 0; continue; }
        if (zeal_partition_u32(b) != 0x41615252u ||
            zeal_partition_u32(b + 484) != 0x61417272u ||
            zeal_partition_u32(b + 508) != 0xaa550000u) return 0;
    }
    root = zeal_partition_u32(boot + 44);
    uint64_t cluster_bytes = (uint64_t)s.sectors_per_cluster * 512;
    uint32_t need = (uint32_t)(((uint64_t)bytes + cluster_bytes - 1) / cluster_bytes);
    if (need > s.largest_run) return 0;
    /* Deliberately reject multi-cluster root directories for this narrow path. */
    if (!read(ctx, s.fat_first + root / 128, a) ||
        (zeal_partition_u32(a + (root % 128) * 4) & 0x0fffffffu) < 0x0ffffff8u) return 0;
    uint64_t root_lba = s.data_first + (uint64_t)(root - 2) * s.sectors_per_cluster;
    int ended = 0, found_slot = 0;
    for (uint32_t j = 0; j < s.sectors_per_cluster; j++) {
        if (!read(ctx, root_lba + j, a)) return 0;
        for (unsigned i = 0; i < 512; i += 32) {
            int first_end = !ended && !a[i];
            if (first_end) ended = 1;
            if (!ended && a[i] != 0xe5 && a[i+11] != 0x0f) {
                unsigned k = 0; while (k < 11 && a[i+k] == name[k]) k++;
                if (k == 11) return 0;
            }
            /* Deleted entries are safe; an end marker needs another slot to
             * retain an end marker, which is outside this limited writer. */
            if (!found_slot && !ended && a[i] == 0xe5) {
                found_slot = 1; dir_sector = root_lba + j; dir_offset = i;
            }
            if (!found_slot && first_end && i <= 448) {
                found_slot = 1; dir_sector = root_lba + j; dir_offset = i;
            }
        }
    }
    if (!found_slot) return 0;
    for (uint32_t c = 2; c <= s.clusters + 1; c++) {
        if (!(c % 128) || c == 2)
            if (!read(ctx, s.fat_first + c / 128, a)) return 0;
        if ((zeal_partition_u32(a + (c % 128) * 4) & 0x0fffffffu) == 0) {
            if (!run) start = c;
            if (++run == need) break;
        } else run = 0;
    }
    if (run != need) return 0;
    uint64_t dest = s.data_first + (uint64_t)(start - 2) * s.sectors_per_cluster;
    uint32_t remaining = bytes, offset = 0;
    for (uint64_t j = 0; j < (uint64_t)need * s.sectors_per_cluster; j++) {
        for (unsigned k = 0; k < 512; k++) a[k] = remaining ? (remaining--, data[offset++]) : 0;
        if (!write(ctx, dest + j, a)) return 0;
    }
    if (!flush(ctx)) return 0;
    remaining = bytes; offset = 0;
    for (uint64_t j = 0; j < (uint64_t)need * s.sectors_per_cluster; j++) {
        if (!read(ctx, dest + j, a)) return 0;
        for (unsigned k = 0; k < 512; k++) {
            uint8_t expected = remaining ? (remaining--, data[offset++]) : 0;
            if (a[k] != expected) return 0;
        }
    }
    for (uint32_t sector = start / 128; sector <= (start + need - 1) / 128; sector++) {
        if (!read(ctx, s.fat_first + sector, a) ||
            !read(ctx, s.fat_first + s.fat_sectors + sector, b)) return 0;
        for (unsigned i = 0; i < 512; i++) if (a[i] != b[i]) return 0;
        for (unsigned i = 0; i < 128; i++) {
            uint32_t c = sector * 128 + i;
            if (c < start || c - start >= need) continue;
            uint32_t value = c - start + 1 == need ? 0x0fffffffu : c + 1;
            value |= zeal_partition_u32(a + i*4) & 0xf0000000u;
            for (unsigned j = 0; j < 4; j++) a[i*4+j] = (uint8_t)(value >> (8*j));
        }
        if (!metadata_write(ctx, s.fat_first + sector, a) ||
            !metadata_write(ctx, s.fat_first + s.fat_sectors + sector, a)) return 0;
    }
    if (!flush(ctx)) return 0;
    for (uint32_t sector = start / 128; sector <= (start + need - 1) / 128; sector++) {
        if (!read(ctx, s.fat_first + sector, a) ||
            !read(ctx, s.fat_first + s.fat_sectors + sector, b)) return 0;
        for (unsigned i = 0; i < 512; i++) if (a[i] != b[i]) return 0;
        for (unsigned i = 0; i < 128; i++) {
            uint32_t c = sector * 128 + i;
            if (c < start || c - start >= need) continue;
            uint32_t expected = c - start + 1 == need ? 0x0fffffffu : c + 1;
            if ((zeal_partition_u32(a + i*4) & 0x0fffffffu) != expected) return 0;
        }
    }
    /* Invalidate cached free-space counts, including the backup FSInfo. */
    for (unsigned copy = 0; copy < 2; copy++) {
        if (copy && !backup_fsinfo) continue;
        uint64_t lba = first + fsinfo + (copy ? backup : 0);
        if (!read(ctx, lba, b) || zeal_partition_u32(b) != 0x41615252u ||
            zeal_partition_u32(b + 484) != 0x61417272u ||
            zeal_partition_u32(b + 508) != 0xaa550000u) return 0;
        for (unsigned i = 488; i < 496; i++) b[i] = 0xff;
        if (!metadata_write(ctx, lba, b)) return 0;
    }
    if (!flush(ctx) || !read(ctx, dir_sector, a)) return 0;
    for (unsigned i = 0; i < 32; i++) a[dir_offset + i] = 0;
    for (unsigned i = 0; i < 11; i++) a[dir_offset+i] = name[i];
    a[dir_offset+11] = 0x20;
    a[dir_offset+20] = (uint8_t)(start >> 16); a[dir_offset+21] = (uint8_t)(start >> 24);
    a[dir_offset+26] = (uint8_t)start; a[dir_offset+27] = (uint8_t)(start >> 8);
    for (unsigned i = 0; i < 4; i++) a[dir_offset+28+i] = (uint8_t)(bytes >> (8*i));
    if (!metadata_write(ctx, dir_sector, a) || !flush(ctx) || !read(ctx, dir_sector, verify)) return 0;
    for (unsigned i = 0; i < 512; i++) if (verify[i] != a[i]) return 0;
    return 1;
}

static int efi_fat32_create_root(zeal_partition_read_fn read, efi_fat32_write_fn write,
                                 efi_fat32_flush_fn flush, void *ctx,
                                 uint64_t first, uint64_t sectors,
                                 const uint8_t name[11], const uint8_t *data, uint32_t bytes) {
    return efi_fat32_create_root_ex(read, write, write, flush, ctx,
                                    first, sectors, name, data, bytes);
}

/* Replace the contents of an existing one-sector root file only if its
 * current bytes exactly match the expected stock configuration. The file's
 * length stays unchanged; the replacement is padded with newlines. */
static int efi_fat32_replace_root_small(zeal_partition_read_fn read,
        efi_fat32_write_fn metadata_write, efi_fat32_flush_fn flush, void *ctx,
        uint64_t first, uint64_t sectors, const uint8_t name[11],
        const uint8_t *expected, uint32_t old_bytes,
        const uint8_t *replacement, uint32_t new_bytes) {
    struct efi_fat32_space s;
    uint8_t boot[512], b[512], fat[512], mirror[512], check[512];
    uint32_t found = 0, file_cluster = 0;
    if (!read || !metadata_write || !flush || !name || !expected || !replacement ||
        !old_bytes || old_bytes > 512 || new_bytes > old_bytes ||
        !efi_fat32_scan_space(read, ctx, first, sectors, &s) || !read(ctx, first, boot)) return 0;
    uint32_t root = zeal_partition_u32(boot + 44);
    if (!read(ctx, s.fat_first + root / 128, fat) ||
        (zeal_partition_u32(fat + (root % 128) * 4) & 0x0fffffffu) < 0x0ffffff8u) return 0;
    uint64_t root_lba = s.data_first + (uint64_t)(root - 2) * s.sectors_per_cluster;
    int ended = 0;
    for (uint32_t j = 0; j < s.sectors_per_cluster && !ended; j++) {
        if (!read(ctx, root_lba + j, b)) return 0;
        for (unsigned i = 0; i < 512; i += 32) {
            if (!b[i]) { ended = 1; break; }
            if (b[i] == 0xe5 || b[i+11] == 0x0f || (b[i+11] & 0x18)) continue;
            unsigned k = 0; while (k < 11 && b[i+k] == name[k]) k++;
            if (k != 11) continue;
            found++;
            file_cluster = ((uint32_t)efi_fat32_u16(b + i + 20) << 16) | efi_fat32_u16(b + i + 26);
            if (zeal_partition_u32(b + i + 28) != old_bytes) return 0;
        }
    }
    if (found != 1 || file_cluster < 2 || file_cluster > s.clusters + 1 || file_cluster == root ||
        !read(ctx, s.fat_first + file_cluster / 128, fat) ||
        !read(ctx, s.fat_first + s.fat_sectors + file_cluster / 128, mirror)) return 0;
    for (unsigned i = 0; i < 512; i++) if (fat[i] != mirror[i]) return 0;
    if ((zeal_partition_u32(fat + (file_cluster % 128) * 4) & 0x0fffffffu) < 0x0ffffff8u) return 0;
    uint64_t lba = s.data_first + (uint64_t)(file_cluster - 2) * s.sectors_per_cluster;
    if (!read(ctx, lba, b)) return 0;
    for (uint32_t i = 0; i < old_bytes; i++) if (b[i] != expected[i]) return 0;
    for (uint32_t i = 0; i < old_bytes; i++) b[i] = i < new_bytes ? replacement[i] : '\n';
    if (!metadata_write(ctx, lba, b) || !flush(ctx) || !read(ctx, lba, check)) return 0;
    for (unsigned i = 0; i < 512; i++) if (b[i] != check[i]) return 0;
    return 1;
}
