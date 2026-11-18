/* ZealOS source module: magic, uncompressed size, CRC32, raw LZ4 block.
 * All integer fields are little endian. */
#pragma once
#include "lz4_block.h"
#define ZEAL_LZ4_HEADER 24u
#define ZEAL_LZ4_MAX_OUTPUT (128u * 1024u * 1024u)

static uint32_t zeal_lz4_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint32_t zeal_lz4_crc(const uint8_t *p, size_t n) {
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < n; i++) {
        crc ^= p[i];
        for (unsigned j = 0; j < 8; j++)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
static int zeal_lz4_is_archive(const uint8_t *p, size_t n) {
    const char *magic = "ZEALLZ41";
    if (!p || n < 8) return 0;
    for (unsigned i = 0; i < 8; i++) if (p[i] != (uint8_t)magic[i]) return 0;
    return 1;
}
static int zeal_lz4_archive_size(const uint8_t *p, size_t n, uint32_t *size) {
    if (!zeal_lz4_is_archive(p, n) || n < ZEAL_LZ4_HEADER ||
        zeal_lz4_u32(p + 12) != n - ZEAL_LZ4_HEADER ||
        zeal_lz4_u32(p + 20) != zeal_lz4_crc(p + ZEAL_LZ4_HEADER, n - ZEAL_LZ4_HEADER)) return 0;
    *size = zeal_lz4_u32(p + 8);
    return *size && *size <= ZEAL_LZ4_MAX_OUTPUT;
}
static int zeal_lz4_archive_decode(const uint8_t *p, size_t n, uint8_t *out, size_t cap) {
    uint32_t size;
    return zeal_lz4_archive_size(p, n, &size) && cap == size &&
        zeal_lz4_block_decode(p + ZEAL_LZ4_HEADER, n - ZEAL_LZ4_HEADER, out, cap) &&
        zeal_lz4_crc(out, cap) == zeal_lz4_u32(p + 16);
}
