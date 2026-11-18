/* Read-only source-bank catalog and byte-for-byte baseline comparison.
 * This code never writes or publishes a source bank. */
#pragma once
#include "efi_source_tar.h"

#define ZU_SOURCE_MAX_FILES 4096u
#define ZU_SOURCE_MAX_BYTES (8u * 1024u * 1024u)
struct zu_source_item {
    char name[96];
    uint32_t header, bytes, blocks, crc;
    uint8_t deleted, baseline_seen, unchanged;
};
struct zu_source_scan {
    zeal_partition_read_fn read;
    void *context;
    uint64_t bank;
    uint32_t count, generation;
    struct zu_source_item items[ZU_SOURCE_MAX_FILES];
};
struct zu_source_stats { uint32_t unchanged, edited, deleted, missing, user_only; };

static int zu_source_equal(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}
static int zu_source_name_ok(const char *s) {
    unsigned length = 0, segment = 0;
    if (!s || !*s) return 0;
    for (; s[length]; length++) {
        unsigned char c = (unsigned char)s[length];
        if (length >= 95 || c < 33 || c > 126 || c == ':' || c == '\\') return 0;
        if (c == '/') { if (!segment) return 0; segment = 0; }
        else segment++;
    }
    if (!segment) return 0;
    for (unsigned start = 0, end; start < length; start = end + 1) {
        for (end = start; end < length && s[end] != '/'; end++) {}
        if ((end - start == 1 && s[start] == '.') ||
            (end - start == 2 && s[start] == '.' && s[start+1] == '.')) return 0;
    }
    return 1;
}
static int zu_source_generation(zeal_partition_read_fn read, void *ctx, uint64_t lba,
                                uint32_t *generation) {
    uint8_t b[512];
    if (!read(ctx, lba, b) || zeal_partition_u32(b + 12) != 65536) return 0;
    const char *v1 = "ZCSRC001", *v2 = "ZCSRC002";
    int a = 1, c = 1;
    for (unsigned i = 0; i < 8; i++) { if (b[i] != (uint8_t)v1[i]) a = 0; if (b[i] != (uint8_t)v2[i]) c = 0; }
    if (a && zeal_partition_u32(b + 8) == 1) { *generation = 0; return 1; }
    if (c && zeal_partition_u32(b + 8) == 2 && zeal_partition_u32(b + 16) &&
        zeal_partition_u32(b + 20) == zeal_partition_crc(b, 20)) {
        *generation = zeal_partition_u32(b + 16); return 1;
    }
    return 0;
}
static int zu_source_catalog(zeal_partition_read_fn read, void *ctx, uint64_t bank_a,
                             uint64_t bank_b, struct zu_source_scan *scan) {
    uint8_t h[512], data[512];
    uint32_t ga = 0, gb = 0, pos = 1;
    if (!read || !scan) return 0;
    int a = zu_source_generation(read, ctx, bank_a, &ga);
    int b = zu_source_generation(read, ctx, bank_b, &gb);
    if (!a && !b) return 0;
    scan->read = read; scan->context = ctx;
    scan->bank = b && (!a || gb > ga) ? bank_b : bank_a;
    scan->generation = b && (!a || gb > ga) ? gb : ga;
    scan->count = 0;
    while (pos < 65536) {
        if (!read(ctx, scan->bank + pos, h)) return 0;
        int empty = 1; for (unsigned i = 0; i < 512; i++) if (h[i]) { empty = 0; break; }
        if (empty) break;
        const char *file = "ZCFILE1", *del = "ZCDEL1";
        int is_file = !h[7], is_del = !h[6] && !h[7];
        for (unsigned i = 0; i < 7; i++) if (h[i] != (uint8_t)file[i]) is_file = 0;
        for (unsigned i = 0; i < 6; i++) if (h[i] != (uint8_t)del[i]) is_del = 0;
        uint32_t length = zeal_partition_u32(h + 8), crc = zeal_partition_u32(h + 12);
        uint32_t blocks = zeal_partition_u32(h + 16), n = zeal_partition_u32(h + 20);
        if ((!is_file && !is_del) || !n || n > 95 || length > ZU_SOURCE_MAX_BYTES ||
            (is_del ? (length || crc || blocks) : blocks != (length + 511u) / 512u) ||
            (uint64_t)pos + 1 + blocks > 65536) return 0;
        char name[96]; for (uint32_t i = 0; i < n; i++) name[i] = (char)h[32+i]; name[n] = 0;
        if (!zu_source_name_ok(name)) return 0;
        uint32_t i = 0; while (i < scan->count && !zu_source_equal(scan->items[i].name, name)) i++;
        if (i == scan->count) { if (scan->count >= ZU_SOURCE_MAX_FILES) return 0; scan->count++; }
        struct zu_source_item *item = &scan->items[i];
        for (uint32_t k = 0; k <= n; k++) item->name[k] = name[k];
        item->header = pos; item->bytes = length; item->blocks = blocks; item->crc = crc;
        item->deleted = (uint8_t)is_del; item->baseline_seen = 0; item->unchanged = 0;
        pos += 1 + blocks;
    }
    /* Validate every latest live record before trusting its contents. */
    for (uint32_t i = 0; i < scan->count; i++) {
        struct zu_source_item *item = &scan->items[i];
        if (item->deleted) continue;
        uint32_t crc = 0xffffffffu, left = item->bytes;
        for (uint32_t b = 0; b < item->blocks; b++) {
            if (!read(ctx, scan->bank + item->header + 1 + b, data)) return 0;
            uint32_t n = left > 512 ? 512 : left;
            for (uint32_t k = 0; k < n; k++) crc = zeal_partition_crc_step(crc, data[k]);
            left -= n;
        }
        if (left || (crc ^ 0xffffffffu) != item->crc) return 0;
    }
    return 1;
}
static int zu_source_baseline_compare(struct zu_source_scan *scan, struct efi_source_tar *tar,
                                      struct zu_source_stats *stats) {
    struct efi_source_tar_entry entry;
    uint8_t archive[512], installed[512];
    if (!scan || !tar || !stats) return 0;
    stats->unchanged = stats->edited = stats->deleted = stats->missing = stats->user_only = 0;
    int status;
    while ((status = efi_source_tar_next(tar, &entry)) == 1) {
        uint32_t i = 0;
        while (i < scan->count && !zu_source_equal(scan->items[i].name, entry.name)) i++;
        struct zu_source_item *item = i < scan->count ? &scan->items[i] : NULL;
        if (item && item->baseline_seen) return 0;
        if (item) item->baseline_seen = 1;
        int equal = item && !item->deleted && item->bytes == entry.bytes;
        uint32_t block = 0;
        while (tar->remaining) {
            uint32_t used;
            if (!efi_source_tar_data(tar, archive, &used)) return 0;
            if (equal) {
                if (!scan->read(scan->context, scan->bank + item->header + 1 + block, installed)) return 0;
                for (uint32_t k = 0; k < used; k++) if (archive[k] != installed[k]) equal = 0;
            }
            block++;
        }
        if (!item) stats->missing++;
        else if (item->deleted) stats->deleted++;
        else if (equal) { item->unchanged = 1; stats->unchanged++; }
        else stats->edited++;
    }
    if (status != 0) return 0;
    for (uint32_t i = 0; i < scan->count; i++)
        if (!scan->items[i].baseline_seen) stats->user_only++;
    return 1;
}
