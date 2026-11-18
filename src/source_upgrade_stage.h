/* Prepare a merged source bank without publishing its superblock.
 * The active bank remains selected until a separate transaction publishes the
 * staged header. This code is diagnostic-only until publication is coordinated
 * with the EFI boot switch and startup recovery. */
#pragma once
#include "source_upgrade_scan.h"
#include "upgrade_commit.h"

struct zu_incoming_item { char name[96]; const uint8_t *data; uint32_t bytes, crc; uint8_t seen; };
struct zu_source_plan_item { uint32_t old_index, incoming_index; uint8_t use_incoming; };
struct zu_source_stage {
    struct zu_incoming_item incoming[ZU_SOURCE_MAX_FILES];
    struct zu_source_plan_item plan[ZU_SOURCE_MAX_FILES];
    uint32_t incoming_count, plan_count, needed, refreshed, preserved, added;
    uint64_t target;
    uint32_t target_generation;
};
static int zu_source_suffix(const char *s) {
    unsigned n = 0; while (s[n]) n++;
    return n >= 3 && s[n-3] == '.' &&
           ((s[n-2] == 'Z' && s[n-1] == 'C') || (s[n-2] == 'H' && s[n-1] == 'H'));
}
static int zu_source_incoming_index(struct zu_source_stage *stage, const uint8_t *tar, size_t bytes) {
    if (!stage || !tar || bytes < 1024 || bytes % 512) return 0;
    stage->incoming_count = 0;
    size_t off = 0;
    while (off <= bytes - 512) {
        const uint8_t *h = tar + off;
        if (efi_tar_zero(h)) {
            if (bytes - off < 1024 || !efi_tar_zero(h + 512)) return 0;
            for (size_t i = off; i < bytes; i++) if (tar[i]) return 0;
            return 1;
        }
        uint64_t size, expected, sum = 0;
        if (h[257] != 'u' || h[258] != 's' || h[259] != 't' || h[260] != 'a' || h[261] != 'r' ||
            !efi_tar_octal(h + 124, 12, &size) || !efi_tar_octal(h + 148, 8, &expected) ||
            size > bytes - off - 512 || (h[156] && h[156] != '0')) return 0;
        for (unsigned i = 0; i < 512; i++) sum += i >= 148 && i < 156 ? ' ' : h[i];
        if (sum != expected) return 0;
        unsigned n = 0, p = 0, k = 0;
        while (n < 100 && h[n]) n++;
        while (p < 155 && h[345+p]) p++;
        if (!n || n == 100 || p == 155 || n + p + !!p >= 96) return 0;
        char name[96];
        for (unsigned i = 0; i < p; i++) name[k++] = (char)h[345+i];
        if (p) name[k++] = '/';
        for (unsigned i = 0; i < n; i++) name[k++] = (char)h[i];
        name[k] = 0;
        if (!zu_source_name_ok(name)) return 0;
        if (zu_source_suffix(name)) {
            if (size > ZU_SOURCE_MAX_BYTES || stage->incoming_count >= ZU_SOURCE_MAX_FILES) return 0;
            for (uint32_t i = 0; i < stage->incoming_count; i++)
                if (zu_source_equal(stage->incoming[i].name, name)) return 0;
            struct zu_incoming_item *item = &stage->incoming[stage->incoming_count++];
            for (unsigned i = 0; i <= k; i++) item->name[i] = name[i];
            item->data = h + 512; item->bytes = (uint32_t)size;
            item->crc = zeal_partition_crc(item->data, item->bytes); item->seen = 0;
        }
        uint64_t padded = (size + 511u) & ~511ull;
        if (padded > bytes - off - 512) return 0;
        off += 512 + (size_t)padded;
    }
    return 0;
}
static int zu_source_make_plan(struct zu_source_scan *scan, struct zu_source_stage *stage) {
    if (!scan || !stage || scan->generation == UINT32_MAX) return 0;
    stage->plan_count = stage->refreshed = stage->preserved = stage->added = 0;
    stage->needed = 1;
    for (uint32_t i = 0; i < scan->count; i++) {
        struct zu_source_item *old = &scan->items[i];
        uint32_t n = 0; while (n < stage->incoming_count &&
                                !zu_source_equal(stage->incoming[n].name, old->name)) n++;
        if (n < stage->incoming_count) stage->incoming[n].seen = 1;
        if (stage->plan_count >= ZU_SOURCE_MAX_FILES) return 0;
        struct zu_source_plan_item *plan = &stage->plan[stage->plan_count++];
        plan->old_index = i; plan->incoming_index = n;
        plan->use_incoming = (uint8_t)(!old->deleted && old->unchanged && n < stage->incoming_count);
        uint32_t blocks = plan->use_incoming ? (stage->incoming[n].bytes + 511u) / 512u : old->blocks;
        if ((uint64_t)stage->needed + 1 + blocks > 65536) return 0;
        stage->needed += 1 + blocks;
        if (plan->use_incoming) stage->refreshed++; else stage->preserved++;
    }
    for (uint32_t n = 0; n < stage->incoming_count; n++) if (!stage->incoming[n].seen) {
        if (stage->plan_count >= ZU_SOURCE_MAX_FILES) return 0;
        uint32_t blocks = (stage->incoming[n].bytes + 511u) / 512u;
        if ((uint64_t)stage->needed + 1 + blocks > 65536) return 0;
        struct zu_source_plan_item *plan = &stage->plan[stage->plan_count++];
        plan->old_index = UINT32_MAX; plan->incoming_index = n; plan->use_incoming = 1;
        stage->needed += 1 + blocks; stage->added++;
    }
    stage->target_generation = scan->generation + 1;
    return 1;
}
static void zu_source_w32(uint8_t *p, uint32_t n) {
    for (unsigned i = 0; i < 4; i++) p[i] = (uint8_t)(n >> (8*i));
}
static int zu_source_stage_sector(struct zu_source_scan *scan, struct zu_source_stage *stage,
                                  const struct zu_source_plan_item *plan, uint32_t block,
                                  uint8_t out[512]) {
    if (!plan->use_incoming) {
        struct zu_source_item *old = &scan->items[plan->old_index];
        return scan->read(scan->context, scan->bank + old->header + block, out);
    }
    struct zu_incoming_item *item = &stage->incoming[plan->incoming_index];
    for (unsigned i = 0; i < 512; i++) out[i] = 0;
    if (!block) {
        const char *magic = "ZCFILE1";
        for (unsigned i = 0; i < 7; i++) out[i] = (uint8_t)magic[i];
        unsigned n = 0; while (item->name[n]) n++;
        zu_source_w32(out + 8, item->bytes); zu_source_w32(out + 12, item->crc);
        zu_source_w32(out + 16, (item->bytes + 511u) / 512u); zu_source_w32(out + 20, n);
        for (unsigned i = 0; i < n; i++) out[32+i] = (uint8_t)item->name[i];
    } else {
        uint64_t offset = (uint64_t)(block - 1) * 512;
        if (offset >= item->bytes) return 0;
        uint32_t n = item->bytes - offset > 512 ? 512 : (uint32_t)(item->bytes - offset);
        for (uint32_t i = 0; i < n; i++) out[i] = item->data[offset+i];
    }
    return 1;
}
/* Writes only an unpublished inactive bank. No source generation changes. */
static int zu_source_stage_inactive(struct zu_source_scan *scan, struct zu_source_stage *stage,
                                    uint64_t bank_a, uint64_t bank_b, uint64_t capacity,
                                    zu_write_fn write, zu_flush_fn flush) {
    uint8_t expected[512], check[512];
    if (!scan || !scan->read || !stage || !write || !flush || !stage->needed ||
        stage->needed > 65536 || bank_a >= capacity || bank_b >= capacity ||
        65536ull > capacity - bank_a || 65536ull > capacity - bank_b ||
        (bank_a < bank_b ? bank_b - bank_a < 65536 : bank_a - bank_b < 65536) ||
        (scan->bank != bank_a && scan->bank != bank_b)) return 0;
    uint64_t target = scan->bank == bank_a ? bank_b : bank_a;
    if (target >= capacity || 65536ull > capacity - target) return 0;
    stage->target = target;
    for (unsigned i = 0; i < 512; i++) expected[i] = 0;
    if (!write(scan->context, target, expected) || !flush(scan->context) ||
        !scan->read(scan->context, target, check)) return 0;
    for (unsigned i = 0; i < 512; i++) if (check[i]) return 0;
    for (unsigned pass = 0; pass < 2; pass++) {
        uint32_t pos = 1;
        for (uint32_t i = 0; i < stage->plan_count; i++) {
            struct zu_source_plan_item *plan = &stage->plan[i];
            uint32_t blocks = plan->use_incoming ? (stage->incoming[plan->incoming_index].bytes + 511u) / 512u :
                              scan->items[plan->old_index].blocks;
            for (uint32_t block = 0; block <= blocks; block++, pos++) {
                if (!zu_source_stage_sector(scan, stage, plan, block, expected)) return 0;
                if (!pass) { if (!write(scan->context, target + pos, expected)) return 0; }
                else {
                    if (!scan->read(scan->context, target + pos, check)) return 0;
                    for (unsigned k = 0; k < 512; k++) if (check[k] != expected[k]) return 0;
                }
            }
        }
        if (pos != stage->needed) return 0;
        if (pos < 65536) {
            for (unsigned k = 0; k < 512; k++) expected[k] = 0;
            if (!pass) { if (!write(scan->context, target + pos, expected)) return 0; }
            else {
                if (!scan->read(scan->context, target + pos, check)) return 0;
                for (unsigned k = 0; k < 512; k++) if (check[k]) return 0;
            }
        }
        if (!pass && !flush(scan->context)) return 0;
    }
    if (!scan->read(scan->context, target, check)) return 0;
    for (unsigned i = 0; i < 512; i++) if (check[i]) return 0;
    return 1;
}
