/* Read-only plan for reclaiming the original EFI source archive. */
#pragma once
#include "efi_fat32_ownership.h"
#define EFI_RECLAIM_MAX_FAT_SECTORS 4096u
struct efi_reclaim_plan { uint32_t clusters, fat_sectors, metadata_records; };
static int efi_fat32_reclaim_plan(zeal_partition_read_fn read, void *ctx,
                                  uint64_t first, uint64_t sectors,
                                  struct efi_fat32_ownership *owner,
                                  struct efi_reclaim_plan *out) {
    struct efi_source_reader source;
    uint8_t seen[EFI_RECLAIM_MAX_FAT_SECTORS / 8] = {0};
    struct efi_reclaim_plan plan = {0};
    if (!out || !efi_fat32_ownership_check(read, ctx, first, sectors, owner) ||
        !efi_source_open(read, ctx, first, sectors, &source) ||
        source.space.fat_sectors > EFI_RECLAIM_MAX_FAT_SECTORS) return 0;
    uint64_t bytes_per_cluster = (uint64_t)source.space.sectors_per_cluster * 512;
    plan.clusters = (uint32_t)(((uint64_t)source.bytes + bytes_per_cluster - 1) / bytes_per_cluster);
    uint32_t cluster = source.first_cluster;
    for (uint32_t i = 0; i < plan.clusters; i++) {
        uint32_t fat_index = cluster / 128, next;
        if (fat_index >= source.space.fat_sectors || !efi_source_next_cluster(&source, cluster, &next)) return 0;
        if (!(seen[fat_index / 8] & (1u << (fat_index % 8)))) {
            seen[fat_index / 8] |= (uint8_t)(1u << (fat_index % 8)); plan.fat_sectors++;
        }
        if (i + 1 == plan.clusters) { if (next < 0x0ffffff8u) return 0; }
        else { if (next < 2 || next > source.space.clusters + 1) return 0; cluster = next; }
    }
    /* Two mirrored FAT sectors per touched sector, plus directory metadata. */
    plan.metadata_records = 2 * plan.fat_sectors + 1; *out = plan; return 1;
}
