/* Conservative FAT32 ownership validation for future space reclamation.
 * Read-only: reject shared clusters, cycles, invalid chains and trees that
 * exceed the bounded workspace. Orphan allocations are left untouched. */
#pragma once
#include "efi_fat32_read.h"
#define EFI_OWNER_MAX_CLUSTERS 262144u
#define EFI_OWNER_MAX_DIRS 1024u
struct efi_fat32_ownership {
    struct efi_source_reader reader;
    uint8_t used[(EFI_OWNER_MAX_CLUSTERS + 9u) / 8u];
    uint32_t directories[EFI_OWNER_MAX_DIRS], count;
};
static int efi_owner_mark(struct efi_fat32_ownership *o, uint32_t cluster) {
    if (cluster < 2 || cluster > o->reader.space.clusters + 1 ||
        cluster >= EFI_OWNER_MAX_CLUSTERS + 2u) return 0;
    uint32_t bit = cluster - 2;
    if (o->used[bit / 8] & (1u << (bit % 8))) return 0;
    o->used[bit / 8] |= (uint8_t)(1u << (bit % 8)); return 1;
}
static int efi_owner_file(struct efi_fat32_ownership *o, uint32_t cluster, uint32_t bytes) {
    if (!bytes) return cluster == 0;
    uint64_t cb = (uint64_t)o->reader.space.sectors_per_cluster * 512;
    uint32_t count = (uint32_t)(((uint64_t)bytes + cb - 1) / cb);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t next;
        if (!efi_owner_mark(o, cluster) || !efi_source_next_cluster(&o->reader, cluster, &next)) return 0;
        if (i + 1 == count) { if (next < 0x0ffffff8u) return 0; }
        else { if (next < 2 || next > o->reader.space.clusters + 1) return 0; cluster = next; }
    }
    return 1;
}
static int efi_owner_directory(struct efi_fat32_ownership *o, uint32_t cluster) {
    uint8_t sector[512]; int ended = 0;
    for (;;) {
        uint32_t next;
        if (!efi_owner_mark(o, cluster) || !efi_source_next_cluster(&o->reader, cluster, &next)) return 0;
        uint64_t lba = o->reader.space.data_first + (uint64_t)(cluster - 2) * o->reader.space.sectors_per_cluster;
        for (uint32_t k = 0; k < o->reader.space.sectors_per_cluster && !ended; k++) {
            if (!o->reader.read(o->reader.context, lba + k, sector)) return 0;
            for (unsigned i = 0; i < 512; i += 32) {
                uint8_t *e = sector + i, attr = e[11];
                if (!e[0]) { ended = 1; break; }
                if (e[0] == 0xe5 || attr == 0x0f) continue;
                if (attr & 0x08) { if (attr != 0x08) return 0; continue; }
                if (attr & 0xc0) return 0;
                if (e[0] == '.') {
                    unsigned start = e[1] == '.' ? 2 : 1;
                    if (!(attr & 0x10)) return 0;
                    for (unsigned n = start; n < 11; n++) if (e[n] != ' ') return 0;
                    continue;
                }
                uint32_t first = ((uint32_t)efi_fat32_u16(e + 20) << 16) | efi_fat32_u16(e + 26);
                uint32_t bytes = zeal_partition_u32(e + 28);
                if (attr & 0x10) {
                    if (bytes || first < 2 || o->count == EFI_OWNER_MAX_DIRS) return 0;
                    o->directories[o->count++] = first;
                } else if (!efi_owner_file(o, first, bytes)) return 0;
            }
        }
        if (next >= 0x0ffffff8u) return 1;
        if (next < 2 || next > o->reader.space.clusters + 1) return 0;
        cluster = next;
    }
}
static int efi_fat32_ownership_check(zeal_partition_read_fn read, void *ctx,
                                     uint64_t first, uint64_t sectors,
                                     struct efi_fat32_ownership *o) {
    uint8_t boot[512];
    if (!o || !read || !efi_fat32_scan_space(read, ctx, first, sectors, &o->reader.space) ||
        o->reader.space.clusters > EFI_OWNER_MAX_CLUSTERS || !read(ctx, first, boot)) return 0;
    for (unsigned i = 0; i < sizeof(o->used); i++) o->used[i] = 0;
    o->reader.read = read; o->reader.context = ctx; o->reader.cached_fat_sector = UINT32_MAX;
    o->count = 1; o->directories[0] = zeal_partition_u32(boot + 44);
    for (uint32_t i = 0; i < o->count; i++)
        if (!efi_owner_directory(o, o->directories[i])) return 0;
    return 1;
}
