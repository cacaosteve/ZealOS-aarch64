/* Reversible, single-sector commit primitive for a future EFI upgrade.
 * The caller must reserve three otherwise-unused sectors and prevent concurrent
 * writes to them and to the target. A durable flush must order all prior writes.
 * Recovery rolls back a pending commit; this is not a FAT filesystem updater. */
#pragma once
#include <stdint.h>
#include "block_partition.h"

typedef int (*zu_write_fn)(void *, uint64_t, const uint8_t[512]);
typedef int (*zu_flush_fn)(void *);
struct zu_commit_io {
    void *context;
    zeal_partition_read_fn read;
    zu_write_fn write;
    zu_flush_fn flush;
    uint64_t capacity, journal;
};
static void zu_put64(uint8_t *p, uint64_t n) {
    for (unsigned i = 0; i < 8; i++) p[i] = (uint8_t)(n >> (i * 8));
}
static void zu_put32(uint8_t *p, uint32_t n) {
    for (unsigned i = 0; i < 4; i++) p[i] = (uint8_t)(n >> (i * 8));
}
static int zu_valid_io(const struct zu_commit_io *io) {
    return io && io->read && io->write && io->flush &&
           io->journal < io->capacity && io->capacity - io->journal >= 3;
}
static int zu_empty(const uint8_t *p) {
    for (unsigned i = 0; i < 512; i++) if (p[i]) return 0;
    return 1;
}
static int zu_clear(const struct zu_commit_io *io) {
    uint8_t zero[512] = {0}, check[512];
    return io->write(io->context, io->journal, zero) && io->flush(io->context) &&
           io->read(io->context, io->journal, check) && zu_empty(check);
}
/* 1 clean or rolled back; 0 damaged metadata or I/O failure. */
static int zu_recover(const struct zu_commit_io *io) {
    uint8_t hdr[512], old[512], check[512];
    static const uint8_t magic[8] = {'Z','U','C','O','M','M','0','1'};
    if (!zu_valid_io(io) || !io->read(io->context, io->journal, hdr)) return 0;
    if (zu_empty(hdr)) return 1;
    for (unsigned i = 0; i < 8; i++) if (hdr[i] != magic[i]) return 0;
    uint64_t target = zeal_partition_u64(hdr + 8);
    if (target >= io->capacity || (target >= io->journal && target - io->journal < 3) ||
        zeal_partition_u32(hdr + 24) != zeal_partition_crc(hdr, 24) ||
        !io->read(io->context, io->journal + 1, old) ||
        zeal_partition_u32(hdr + 16) != zeal_partition_crc(old, 512)) return 0;
    if (!io->write(io->context, target, old) || !io->flush(io->context) ||
        !io->read(io->context, target, check)) return 0;
    for (unsigned i = 0; i < 512; i++) if (check[i] != old[i]) return 0;
    return zu_clear(io);
}
/* Caller has staged and verified all new files before invoking this. The
 * target is the one-sector reference that selects the new boot configuration. */
static int zu_commit(const struct zu_commit_io *io, uint64_t target,
                     const uint8_t replacement[512]) {
    uint8_t hdr[512] = {0}, old[512], check[512];
    static const uint8_t magic[8] = {'Z','U','C','O','M','M','0','1'};
    if (!zu_valid_io(io) || !replacement || target >= io->capacity ||
        (target >= io->journal && target - io->journal < 3) ||
        !io->read(io->context, io->journal, check) || !zu_empty(check) ||
        !io->read(io->context, target, old)) return 0;
    /* The original and replacement are durable before the pending marker. */
    if (!io->write(io->context, io->journal + 1, old) ||
        !io->write(io->context, io->journal + 2, replacement) || !io->flush(io->context) ||
        !io->read(io->context, io->journal + 1, check)) return 0;
    for (unsigned i = 0; i < 512; i++) if (check[i] != old[i]) return 0;
    if (!io->read(io->context, io->journal + 2, check)) return 0;
    for (unsigned i = 0; i < 512; i++) if (check[i] != replacement[i]) return 0;
    for (unsigned i = 0; i < 8; i++) hdr[i] = magic[i];
    zu_put64(hdr + 8, target);
    zu_put32(hdr + 16, zeal_partition_crc(old, 512));
    zu_put32(hdr + 20, zeal_partition_crc(replacement, 512));
    zu_put32(hdr + 24, zeal_partition_crc(hdr, 24));
    if (!io->write(io->context, io->journal, hdr) || !io->flush(io->context) ||
        !io->read(io->context, io->journal, check)) return 0;
    for (unsigned i = 0; i < 512; i++) if (check[i] != hdr[i]) return 0;
    if (!io->write(io->context, target, replacement) || !io->flush(io->context) ||
        !io->read(io->context, target, check)) return 0;
    for (unsigned i = 0; i < 512; i++) if (check[i] != replacement[i]) return 0;
    return zu_clear(io);
}
