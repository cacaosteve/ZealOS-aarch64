/* Bounded sequential reader for the stock EFI /boot/sources.tar baseline. */
#pragma once
#include "efi_fat32.h"

struct efi_source_reader {
    zeal_partition_read_fn read;
    void *context;
    struct efi_fat32_space space;
    uint32_t root, boot_dir, first_cluster, current_cluster;
    uint32_t bytes, sectors_left, in_cluster;
    uint32_t cached_fat_sector;
    uint8_t fat_cache[512];
};

static int efi_source_next_cluster(struct efi_source_reader *r, uint32_t cluster,
                                   uint32_t *next) {
    uint8_t mirror[512];
    if (!r || !next || cluster < 2 || cluster > r->space.clusters + 1) return 0;
    uint32_t index = cluster / 128;
    if (index != r->cached_fat_sector) {
        if (!r->read(r->context, r->space.fat_first + index, r->fat_cache) ||
            !r->read(r->context, r->space.fat_first + r->space.fat_sectors + index, mirror)) return 0;
        for (unsigned i = 0; i < 512; i++) if (r->fat_cache[i] != mirror[i]) return 0;
        r->cached_fat_sector = index;
    }
    *next = zeal_partition_u32(r->fat_cache + (cluster % 128) * 4) & 0x0fffffffu;
    return 1;
}

/* Search an 8.3 entry, rejecting duplicate matches and invalid chains. */
static int efi_source_find(struct efi_source_reader *r, uint32_t directory,
                           const uint8_t name[11], int want_dir,
                           uint32_t *cluster_out, uint32_t *bytes_out) {
    uint8_t sector[512];
    uint32_t cluster = directory, found = 0, result_cluster = 0, bytes = 0;
    int ended = 0;
    for (uint32_t visited = 0; visited < r->space.clusters && !ended; visited++) {
        if (cluster < 2 || cluster > r->space.clusters + 1) return 0;
        uint64_t base = r->space.data_first + (uint64_t)(cluster - 2) * r->space.sectors_per_cluster;
        for (uint32_t j = 0; j < r->space.sectors_per_cluster && !ended; j++) {
            if (!r->read(r->context, base + j, sector)) return 0;
            for (unsigned i = 0; i < 512; i += 32) {
                if (!sector[i]) { ended = 1; break; }
                uint8_t attr = sector[i+11];
                if (sector[i] == 0xe5 || attr == 0x0f || (attr & 0x08)) continue;
                unsigned k = 0; while (k < 11 && sector[i+k] == name[k]) k++;
                if (k != 11) continue;
                if (!!(attr & 0x10) != !!want_dir || (attr & 0xc0)) return 0;
                found++;
                result_cluster = ((uint32_t)efi_fat32_u16(sector + i + 20) << 16) |
                                  efi_fat32_u16(sector + i + 26);
                bytes = zeal_partition_u32(sector + i + 28);
            }
        }
        if (!ended) {
            uint32_t next;
            if (!efi_source_next_cluster(r, cluster, &next)) return 0;
            if (next >= 0x0ffffff8u) ended = 1;
            else cluster = next;
        }
    }
    if (!ended || found != 1 || result_cluster < 2 || result_cluster > r->space.clusters + 1) return 0;
    *cluster_out = result_cluster;
    *bytes_out = bytes;
    return 1;
}

static int efi_source_open_kind(zeal_partition_read_fn read, void *context,
                           uint64_t first, uint64_t sectors, struct efi_source_reader *r, int compressed) {
    static const uint8_t boot_name[11] = {'B','O','O','T',' ',' ',' ',' ',' ',' ',' '};
    static const uint8_t source_name[11] = {'S','O','U','R','C','E','S',' ','T','A','R'};
    static const uint8_t compressed_names[2][11] = {
        {'Z','U','S','O','U','R','C','E','L','Z','4'},
        {'Z','V','S','O','U','R','C','E','L','Z','4'}};
    uint8_t boot[512];
    uint32_t ignored, cluster, next;
    if (!r || !read || !efi_fat32_scan_space(read, context, first, sectors, &r->space) ||
        !read(context, first, boot)) return 0;
    r->read = read; r->context = context; r->cached_fat_sector = UINT32_MAX;
    r->root = zeal_partition_u32(boot + 44);
    r->boot_dir = 0;
    if (compressed) {
        if (compressed > 2 || !efi_source_find(r, r->root, compressed_names[compressed - 1],
                                                0, &r->first_cluster, &r->bytes)) return 0;
    } else if (!efi_source_find(r, r->root, boot_name, 1, &r->boot_dir, &ignored) ||
               r->boot_dir == r->root ||
               !efi_source_find(r, r->boot_dir, source_name, 0, &r->first_cluster, &r->bytes)) return 0;
    if (!r->bytes || (!compressed && r->bytes % 512) || r->bytes > 128u * 1024u * 1024u) return 0;
    uint64_t cluster_bytes = (uint64_t)r->space.sectors_per_cluster * 512;
    uint32_t count = (uint32_t)(((uint64_t)r->bytes + cluster_bytes - 1) / cluster_bytes);
    cluster = r->first_cluster;
    for (uint32_t i = 0; i < count; i++) {
        if (cluster == r->root || cluster == r->boot_dir ||
            !efi_source_next_cluster(r, cluster, &next)) return 0;
        if (i + 1 == count) { if (next < 0x0ffffff8u) return 0; }
        else { if (next < 2 || next > r->space.clusters + 1) return 0; cluster = next; }
    }
    r->current_cluster = r->first_cluster;
    r->sectors_left = (r->bytes + 511u) / 512;
    r->in_cluster = 0;
    r->cached_fat_sector = UINT32_MAX;
    return 1;
}
static int efi_source_open(zeal_partition_read_fn read, void *context,
                           uint64_t first, uint64_t sectors, struct efi_source_reader *r) {
    return efi_source_open_kind(read, context, first, sectors, r, 0);
}
static int efi_source_open_compressed(zeal_partition_read_fn read, void *context,
                           uint64_t first, uint64_t sectors, struct efi_source_reader *r) {
    return efi_source_open_kind(read, context, first, sectors, r, 1);
}
static int efi_source_open_compressed_v(zeal_partition_read_fn read, void *context,
                           uint64_t first, uint64_t sectors, struct efi_source_reader *r) {
    return efi_source_open_kind(read, context, first, sectors, r, 2);
}

static int efi_source_read_next(struct efi_source_reader *r, uint8_t out[512]) {
    if (!r || !out || !r->sectors_left) return 0;
    uint64_t lba = r->space.data_first + (uint64_t)(r->current_cluster - 2) *
                   r->space.sectors_per_cluster + r->in_cluster;
    if (!r->read(r->context, lba, out)) return 0;
    r->sectors_left--;
    if (++r->in_cluster == r->space.sectors_per_cluster && r->sectors_left) {
        uint32_t next;
        if (!efi_source_next_cluster(r, r->current_cluster, &next) ||
            next < 2 || next > r->space.clusters + 1) return 0;
        r->current_cluster = next; r->in_cluster = 0;
    }
    return 1;
}
