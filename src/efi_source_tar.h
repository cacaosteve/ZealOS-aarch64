/* Streaming, bounds-checked ustar reader over the EFI baseline archive. */
#pragma once
#include "efi_fat32_read.h"
#include "lz4_stream.h"

struct efi_source_tar {
    struct efi_source_reader *reader;
    uint32_t remaining;
    int finished;
    struct zeal_lz4_stream *compressed;
};
struct efi_source_tar_entry { char name[96]; uint32_t bytes; uint8_t type; };

struct efi_compressed_source {
    struct efi_source_reader reader;
    struct zeal_lz4_stream stream;
    uint8_t sector[512];
    uint32_t offset, left;
};
static int efi_compressed_read_byte(void *context, uint8_t *out) {
    struct efi_compressed_source *s = context;
    if (!s->left) return 0;
    if (s->offset == 512) {
        if (!efi_source_read_next(&s->reader, s->sector)) return 0;
        s->offset = 0;
    }
    *out = s->sector[s->offset++]; s->left--; return 1;
}
static int efi_compressed_source_open_slot(zeal_partition_read_fn read, void *context,
                                      uint64_t first, uint64_t sectors,
                                      struct efi_compressed_source *s, struct efi_source_tar *tar, int slot_v) {
    if (!s || !tar || !(slot_v ? efi_source_open_compressed_v(read, context, first, sectors, &s->reader) :
                                 efi_source_open_compressed(read, context, first, sectors, &s->reader))) return 0;
    s->offset = 512; s->left = s->reader.bytes;
    if (!zeal_lz4_stream_open(&s->stream, efi_compressed_read_byte, s, s->reader.bytes)) return 0;
    tar->reader = &s->reader; tar->remaining = 0; tar->finished = 0; tar->compressed = &s->stream;
    return 1;
}
static int efi_compressed_source_open(zeal_partition_read_fn read, void *context,
                                      uint64_t first, uint64_t sectors,
                                      struct efi_compressed_source *s, struct efi_source_tar *tar) {
    return efi_compressed_source_open_slot(read, context, first, sectors, s, tar, 0);
}
static uint32_t efi_tar_sectors_left(const struct efi_source_tar *tar) {
    return tar->compressed ? (tar->compressed->output_size - tar->compressed->output_pos) / 512 :
                             tar->reader->sectors_left;
}
static int efi_tar_read_sector(struct efi_source_tar *tar, uint8_t out[512]) {
    if (!tar->compressed) return efi_source_read_next(tar->reader, out);
    for (unsigned i = 0; i < 512; i++)
        if (zeal_lz4_stream_byte(tar->compressed, out + i) != 1) return 0;
    return 1;
}

static int efi_tar_octal(const uint8_t *p, unsigned n, uint64_t *out) {
    uint64_t value = 0;
    int digits = 0, ended = 0;
    for (unsigned i = 0; i < n; i++) {
        if (!p[i] || p[i] == ' ') { if (digits) ended = 1; continue; }
        if (ended || p[i] < '0' || p[i] > '7' || value > (UINT64_MAX - 7) / 8) return 0;
        value = value * 8 + p[i] - '0'; digits = 1;
    }
    *out = value;
    return 1;
}
static int efi_tar_zero(const uint8_t b[512]) {
    for (unsigned i = 0; i < 512; i++) if (b[i]) return 0;
    return 1;
}
/* 1=entry, 0=complete archive, -1=malformed or read failure. */
static int efi_source_tar_next(struct efi_source_tar *tar, struct efi_source_tar_entry *entry) {
    uint8_t h[512];
    uint64_t size, checksum, sum = 0;
    if (!tar || !tar->reader || !entry || tar->remaining) return -1;
    if (tar->finished) return 0;
    if (!efi_tar_read_sector(tar, h)) return -1;
    if (efi_tar_zero(h)) {
        if (!efi_tar_read_sector(tar, h) || !efi_tar_zero(h)) return -1;
        while (efi_tar_sectors_left(tar))
            if (!efi_tar_read_sector(tar, h) || !efi_tar_zero(h)) return -1;
        if (tar->compressed) {
            uint8_t byte;
            if (zeal_lz4_stream_byte(tar->compressed, &byte) != 0) return -1;
        }
        tar->finished = 1; return 0;
    }
    if (h[257] != 'u' || h[258] != 's' || h[259] != 't' || h[260] != 'a' || h[261] != 'r' ||
        !efi_tar_octal(h + 124, 12, &size) || !efi_tar_octal(h + 148, 8, &checksum) ||
        size > UINT32_MAX || size > (uint64_t)efi_tar_sectors_left(tar) * 512) return -1;
    for (unsigned i = 0; i < 512; i++) sum += i >= 148 && i < 156 ? ' ' : h[i];
    if (sum != checksum || (h[156] && h[156] != '0')) return -1;
    unsigned n = 0, p = 0, k = 0;
    while (n < 100 && h[n]) n++;
    while (p < 155 && h[345+p]) p++;
    if (!n || n == 100 || p == 155 || n + p + !!p >= sizeof(entry->name)) return -1;
    for (unsigned i = 0; i < p; i++) entry->name[k++] = (char)h[345+i];
    if (p) entry->name[k++] = '/';
    for (unsigned i = 0; i < n; i++) entry->name[k++] = (char)h[i];
    entry->name[k] = 0; entry->bytes = (uint32_t)size; entry->type = h[156];
    tar->remaining = (uint32_t)size;
    return 1;
}
/* Read the next 512-byte archive sector; used is the number of file bytes.
 * Padding is consumed but excluded from used. */
static int efi_source_tar_data(struct efi_source_tar *tar, uint8_t out[512], uint32_t *used) {
    if (!tar || !tar->reader || !out || !used || !tar->remaining ||
        !efi_tar_read_sector(tar, out)) return 0;
    *used = tar->remaining > 512 ? 512 : tar->remaining;
    tar->remaining -= *used;
    return 1;
}
