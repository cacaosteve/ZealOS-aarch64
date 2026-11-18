/* Deterministic expansion of the legacy installer template. No disk writes.
 * Larger targets retain the EFI partition and put RedSea plus the two seeded
 * source banks in one partition, using the same bounded backend as Pi. */
#pragma once
#include "disk_layout.h"
#include "block_partition.h"

typedef int (*zi_read_template)(void *, uint64_t, uint8_t *);
struct zi_layout {
    uint64_t sectors, redsea, bitmap, bank_a, bank_b;
    uint8_t mbr[512], primary[512], backup[512], entries[16384];
    zi_read_template read;
    void *context;
};
static void zi_put(uint8_t *p, uint64_t n, unsigned bytes) {
    for (unsigned i = 0; i < bytes; i++) p[i] = (uint8_t)(n >> (8 * i));
}
static void zi_zero(uint8_t *p, unsigned bytes) {
    for (unsigned i = 0; i < bytes; i++) p[i] = 0;
}
static void zi_copy(uint8_t *d, const uint8_t *s, unsigned bytes) {
    for (unsigned i = 0; i < bytes; i++) d[i] = s[i];
}
static void zi_header_crc(uint8_t *p) {
    zi_put(p + 16, 0, 4);
    zi_put(p + 16, zeal_partition_crc(p, 92), 4);
}
static int zi_layout_init(struct zi_layout *l, uint64_t sectors,
                          zi_read_template read, void *context) {
    /* Signed RedSea byte counts must remain representable. */
    if (!l || !read || sectors < ZEAL_DISK_SECTS ||
        sectors > 0x7fffffffffffffffull / 512) return 0;
    l->sectors = sectors; l->read = read; l->context = context;
    if (sectors == ZEAL_DISK_SECTS) return 1;
    l->bank_b = sectors - 33 - ZEAL_SRC_SHADOW_SECTS;
    l->bank_a = l->bank_b - ZEAL_SRC_SECTS;
    l->redsea = l->bank_a - ZEAL_RS_LBA_BASE;
    l->bitmap = (l->redsea + 4095) / 4096;
    if (l->bitmap + 11 >= l->redsea ||
        !read(context, 0, l->mbr) || !read(context, 1, l->primary) ||
        !read(context, 2, l->entries)) return 0;
    /* Only the pinned EFI entry survives. Never copy legacy source entries. */
    if (zeal_partition_u64(l->entries + 32) != 2048 ||
        zeal_partition_u64(l->entries + 40) != ZEAL_RS_LBA_BASE - 1 ||
        zeal_partition_u32(l->primary + 12) != 92 ||
        zeal_partition_u32(l->primary + 80) != 128 ||
        zeal_partition_u32(l->primary + 84) != 128) return 0;
    zi_zero(l->entries + 128, sizeof(l->entries) - 128);
    uint8_t *entry = l->entries + 128;
    zi_copy(entry, ZEAL_PARTITION_GPT_ZEAL_DATA_GUID, 16);
    zi_copy(entry + 16, l->primary + 56, 16);
    entry[31] ^= 0x80; /* partition UUID distinct from disk UUID */
    zi_put(entry + 32, ZEAL_RS_LBA_BASE, 8);
    zi_put(entry + 40, sectors - 34, 8);
    const char *name = "ZealOS data";
    for (unsigned i = 0; name[i]; i++) entry[56 + i * 2] = name[i];
    zi_put(l->mbr + 458, sectors - 1 > 0xffffffffu ? 0xffffffffu : sectors - 1, 4);
    zi_put(l->primary + 32, sectors - 1, 8);
    zi_put(l->primary + 48, sectors - 34, 8);
    zi_put(l->primary + 88, zeal_partition_crc(l->entries, sizeof(l->entries)), 4);
    zi_header_crc(l->primary);
    zi_copy(l->backup, l->primary, 512);
    zi_put(l->backup + 24, sectors - 1, 8);
    zi_put(l->backup + 32, 1, 8);
    zi_put(l->backup + 72, sectors - 33, 8);
    zi_header_crc(l->backup);
    return 1;
}
static void zi_dirent(uint8_t *p, const char *name, uint64_t cluster, uint64_t size) {
    zi_put(p, 0x410, 2);
    for (unsigned i = 0; name[i]; i++) p[2 + i] = name[i];
    zi_put(p + 40, cluster, 8); zi_put(p + 48, size, 8);
}
static int zi_layout_sector(struct zi_layout *l, uint64_t sector, uint8_t *out) {
    if (!l || !out || sector >= l->sectors) return 0;
    if (l->sectors == ZEAL_DISK_SECTS) return l->read(l->context, sector, out);
    if (!sector) { zi_copy(out, l->mbr, 512); return 1; }
    if (sector == 1) { zi_copy(out, l->primary, 512); return 1; }
    if (sector == l->sectors - 1) { zi_copy(out, l->backup, 512); return 1; }
    if (sector >= 2 && sector < 34) {
        zi_copy(out, l->entries + (sector - 2) * 512, 512); return 1;
    }
    if (sector >= l->sectors - 33) {
        zi_copy(out, l->entries + (sector - (l->sectors - 33)) * 512, 512); return 1;
    }
    if (sector < ZEAL_RS_LBA_BASE) return l->read(l->context, sector, out);
    if (sector >= l->bank_b)
        return l->read(l->context, ZEAL_SRC_SHADOW_LBA + sector - l->bank_b, out);
    if (sector >= l->bank_a)
        return l->read(l->context, ZEAL_SRC_LBA_BASE + sector - l->bank_a, out);
    zi_zero(out, 512);
    uint64_t rel = sector - ZEAL_RS_LBA_BASE;
    uint64_t root = l->bitmap + 1, home = root + 1, tmp = home + 8;
    if (!rel) {
        out[3] = 0x88;
        zi_put(out + 16, l->redsea, 8); zi_put(out + 24, root, 8);
        zi_put(out + 32, l->bitmap, 8);
        zi_put(out + 40, zeal_partition_u64(l->primary + 56) | 1, 8);
        out[510] = 0x55; out[511] = 0xaa;
    } else if (rel == 1) {
        /* RedSea bitmap indexes clusters relative to the bitmap length. */
        zi_put(out, (1u << (tmp + 1 - l->bitmap)) - 1u, 2);
    } else if (rel == root) {
        zi_dirent(out, ".", root, 512); zi_dirent(out + 64, "..", root, 0);
        zi_dirent(out + 128, "Home", home, 4096); zi_dirent(out + 192, "Tmp", tmp, 512);
    } else if (rel == home || rel == tmp) {
        zi_dirent(out, ".", rel, rel == home ? 4096 : 512);
        zi_dirent(out + 64, "..", root, 0);
    }
    return 1;
}
