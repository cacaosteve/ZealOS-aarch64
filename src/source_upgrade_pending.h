/* Experimental deferred source-bank publication.
 * A marker is placed outside the EFI journal. Only the newly booted kernel
 * may publish the previously verified inactive bank after resolving that
 * journal. This helper is not wired into normal startup. */
#pragma once
#include "source_upgrade_scan.h"
#include "upgrade_commit.h"
#include "upgrade_journal.h"

#define ZU_PENDING_PRIMARY 1988ull
#define ZU_PENDING_SHADOW  1989ull
struct zu_source_pending {
    uint64_t active, candidate, capacity;
    uint32_t old_generation, new_generation, used_sectors, active_crc, candidate_crc;
};
static uint32_t zu_pending_u32(const uint8_t *p) { return zeal_partition_u32(p); }
static void zu_pending_encode(uint8_t b[512], const struct zu_source_pending *p) {
    for (unsigned i = 0; i < 512; i++) b[i] = 0;
    const char *magic = "ZUPEND01";
    for (unsigned i = 0; i < 8; i++) b[i] = (uint8_t)magic[i];
    zu_put64(b + 8, p->active); zu_put64(b + 16, p->candidate); zu_put64(b + 24, p->capacity);
    zu_put32(b + 32, p->old_generation); zu_put32(b + 36, p->new_generation);
    zu_put32(b + 40, p->used_sectors); zu_put32(b + 44, p->active_crc);
    zu_put32(b + 48, p->candidate_crc); zu_put32(b + 52, zeal_partition_crc(b, 52));
}
static int zu_pending_decode(const uint8_t b[512], uint64_t capacity, uint64_t a, uint64_t c,
                             struct zu_source_pending *out) {
    const char *magic = "ZUPEND01";
    for (unsigned i = 0; i < 8; i++) if (b[i] != (uint8_t)magic[i]) return 0;
    uint64_t active = zeal_partition_u64(b + 8), candidate = zeal_partition_u64(b + 16);
    uint32_t old = zu_pending_u32(b + 32), next = zu_pending_u32(b + 36), used = zu_pending_u32(b + 40);
    if (!out || zeal_partition_u64(b + 24) != capacity ||
        !((active == a && candidate == c) || (active == c && candidate == a)) ||
        active >= capacity || candidate >= capacity || 65536ull > capacity - active ||
        65536ull > capacity - candidate || old == UINT32_MAX || next != old + 1 ||
        used < 1 || used > 65536 || zu_pending_u32(b + 52) != zeal_partition_crc(b, 52)) return 0;
    out->active = active; out->candidate = candidate; out->capacity = capacity;
    out->old_generation = old; out->new_generation = next; out->used_sectors = used;
    out->active_crc = zu_pending_u32(b + 44); out->candidate_crc = zu_pending_u32(b + 48);
    return 1;
}
static int zu_pending_cancel_decode(const uint8_t b[512], uint64_t capacity, uint64_t a, uint64_t c,
                                    struct zu_source_pending *out) {
    uint8_t original[512];
    const char *magic = "ZUCANC01", *pending = "ZUPEND01";
    for (unsigned i = 0; i < 8; i++) if (b[i] != (uint8_t)magic[i]) return 0;
    if (zu_pending_u32(b + 52) != zeal_partition_crc(b, 52)) return 0;
    for (unsigned i = 0; i < 512; i++) original[i] = b[i];
    for (unsigned i = 0; i < 8; i++) original[i] = (uint8_t)pending[i];
    zu_put32(original + 52, zeal_partition_crc(original, 52));
    return zu_pending_decode(original, capacity, a, c, out);
}
static int zu_pending_range_crc(zeal_partition_read_fn read, void *ctx, uint64_t first,
                                uint32_t count, uint32_t *out) {
    uint8_t b[512]; uint32_t crc = 0xffffffffu;
    if (!read || !out) return 0;
    for (uint32_t i = 0; i < count; i++) {
        if (!read(ctx, first + i, b)) return 0;
        for (unsigned k = 0; k < 512; k++) crc = zeal_partition_crc_step(crc, b[k]);
    }
    *out = crc ^ 0xffffffffu;
    return 1;
}
static int zu_pending_prepare(zeal_partition_read_fn read, zu_write_fn write, zu_flush_fn flush,
                              void *ctx, const struct zu_source_pending *p) {
    uint8_t h[512], check[512], a[512], b[512]; struct zu_source_pending prior;
    if (!read || !write || !flush || !p || p->capacity <= ZU_PENDING_SHADOW ||
        !read(ctx, ZU_PENDING_PRIMARY, a) || !read(ctx, ZU_PENDING_SHADOW, b)) return 0;
    /* A durable cancellation can be replaced by the next preparation. A
     * mixed cancelled/pending pair represents an interrupted transition. */
    if (!(zu_empty(a) && zu_empty(b)) &&
        !zu_pending_cancel_decode(a, p->capacity, p->active, p->candidate, &prior) &&
        !zu_pending_cancel_decode(b, p->capacity, p->active, p->candidate, &prior)) return 0;
    zu_pending_encode(h, p);
    if (!write(ctx, ZU_PENDING_SHADOW, h) || !flush(ctx) || !read(ctx, ZU_PENDING_SHADOW, check)) return 0;
    for (unsigned i = 0; i < 512; i++) if (h[i] != check[i]) return 0;
    if (!write(ctx, ZU_PENDING_PRIMARY, h) || !flush(ctx) || !read(ctx, ZU_PENDING_PRIMARY, check)) return 0;
    for (unsigned i = 0; i < 512; i++) if (h[i] != check[i]) return 0;
    return 1;
}
/* 2=cancelled, 1=valid pending marker, 0=both empty, -1=damaged. */
static int zu_pending_read(zeal_partition_read_fn read, void *ctx, uint64_t capacity,
                           uint64_t a, uint64_t b, struct zu_source_pending *out) {
    uint8_t x[512], y[512]; struct zu_source_pending px, py;
    if (!read || !out || capacity <= ZU_PENDING_SHADOW ||
        !read(ctx, ZU_PENDING_PRIMARY, x) || !read(ctx, ZU_PENDING_SHADOW, y)) return -1;
    if (zu_empty(x) && zu_empty(y)) return 0;
    int cx = zu_pending_cancel_decode(x, capacity, a, b, &px);
    int cy = zu_pending_cancel_decode(y, capacity, a, b, &py);
    if (cx || cy) { *out = cx ? px : py; return 2; }
    int vx = zu_pending_decode(x, capacity, a, b, &px), vy = zu_pending_decode(y, capacity, a, b, &py);
    if (!vx && !vy) return -1;
    if (vx && vy) for (unsigned i = 0; i < 512; i++) if (x[i] != y[i]) return -1;
    *out = vx ? px : py;
    return 1;
}
/* Discard a prepared bank after rollback. Publish two cancellation records
 * instead of erasing the last marker, keeping one recognizable copy through
 * every torn write. A subsequent preparation replaces these records. */
static int zu_pending_cancel(struct zu_journal *j, const struct zu_source_pending *p) {
    uint8_t b[512], zero[512]; uint32_t generation, active_crc;
    if (!j || !p || !zu_valid_io(&j->io) || j->slots != ZU_JOURNAL_MAX_SLOTS ||
        j->io.journal != 34 || p->capacity != j->io.capacity ||
        !j->io.read(j->io.context, j->io.journal, b) || !zu_empty(b) ||
        !j->io.read(j->io.context, j->io.journal + 2 + 3ull*j->slots, b) || !zu_empty(b) ||
        zu_journal_committed(j) != 0 ||
        !zu_source_generation(j->io.read, j->io.context, p->active, &generation) ||
        generation != p->old_generation ||
        !zu_pending_range_crc(j->io.read, j->io.context, p->active, 65536, &active_crc) ||
        active_crc != p->active_crc ||
        zu_source_generation(j->io.read, j->io.context, p->candidate, &generation)) return 0;
    for (unsigned i = 0; i < 512; i++) zero[i] = 0;
    /* This region was verified empty before publishing the pending marker. */
    for (uint64_t lba = j->io.journal + 1; lba <= j->io.journal + 1 + 3ull*j->slots; lba++) {
        if (!j->io.write(j->io.context, lba, zero)) return 0;
    }
    if (!j->io.flush(j->io.context)) return 0;
    for (uint64_t lba = j->io.journal + 1; lba <= j->io.journal + 1 + 3ull*j->slots; lba++)
        if (!j->io.read(j->io.context, lba, b) || !zu_empty(b)) return 0;
    zu_pending_encode(zero, p);
    const char *magic = "ZUCANC01";
    for (unsigned i = 0; i < 8; i++) zero[i] = (uint8_t)magic[i];
    zu_put32(zero + 52, zeal_partition_crc(zero, 52));
    for (uint64_t lba = ZU_PENDING_PRIMARY; lba <= ZU_PENDING_SHADOW; lba++) {
        if (!j->io.write(j->io.context, lba, zero) || !j->io.flush(j->io.context) ||
            !j->io.read(j->io.context, lba, b)) return 0;
        for (unsigned i = 0; i < 512; i++) if (b[i] != zero[i]) return 0;
    }
    return 1;
}
/* Publication is idempotent under interrupted sector writes. Caller must
 * first resolve any EFI journal and ensure this is the new boot path. */
static int zu_pending_publish(zeal_partition_read_fn read, zu_write_fn write, zu_flush_fn flush,
                              void *ctx, const struct zu_source_pending *p) {
    uint8_t header[512], check[512]; uint32_t old_generation, active_crc, candidate_crc;
    if (!read || !write || !flush || !p ||
        !zu_source_generation(read, ctx, p->active, &old_generation) || old_generation != p->old_generation ||
        !zu_pending_range_crc(read, ctx, p->active, 65536, &active_crc) || active_crc != p->active_crc ||
        !zu_pending_range_crc(read, ctx, p->candidate + 1,
                              p->used_sectors < 65536 ? p->used_sectors : p->used_sectors - 1,
                              &candidate_crc) || candidate_crc != p->candidate_crc) return 0;
    for (unsigned i = 0; i < 512; i++) header[i] = 0;
    const char *magic = "ZCSRC002";
    for (unsigned i = 0; i < 8; i++) header[i] = (uint8_t)magic[i];
    zu_put32(header + 8, 2); zu_put32(header + 12, 65536);
    zu_put32(header + 16, p->new_generation); zu_put32(header + 20, zeal_partition_crc(header, 20));
    if (!write(ctx, p->candidate, header) || !flush(ctx) || !read(ctx, p->candidate, check)) return 0;
    for (unsigned i = 0; i < 512; i++) if (check[i] != header[i]) return 0;
    for (unsigned i = 0; i < 512; i++) header[i] = 0;
    if (!write(ctx, ZU_PENDING_PRIMARY, header) || !flush(ctx) || !read(ctx, ZU_PENDING_PRIMARY, check) ||
        !zu_empty(check)) return 0;
    if (!write(ctx, ZU_PENDING_SHADOW, header) || !flush(ctx) || !read(ctx, ZU_PENDING_SHADOW, check) ||
        !zu_empty(check)) return 0;
    return 1;
}
