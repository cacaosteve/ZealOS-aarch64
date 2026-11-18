#include <stdio.h>
#include <string.h>
#include "../src/lz4_archive.h"

static void put32(uint8_t *p, uint32_t n) {
    for (unsigned i = 0; i < 4; i++) p[i] = (uint8_t)(n >> (i * 8));
}

int main(void) {
    uint8_t out[64];
    const uint8_t literal[] = {0x50, 'h','e','l','l','o'};
    const uint8_t repeat[] = {0x11, 'a', 1, 0, 0x10, '!'};
    const uint8_t bad_offset[] = {0x10, 'a', 0, 0};
    const uint8_t truncated[] = {0xf0, 255};
    uint8_t archive[30] = {'Z','E','A','L','L','Z','4','1'};
    memcpy(archive + ZEAL_LZ4_HEADER, literal, sizeof(literal));
    put32(archive + 8, 5);
    put32(archive + 12, sizeof(literal));
    put32(archive + 16, zeal_lz4_crc((const uint8_t *)"hello", 5));
    put32(archive + 20, zeal_lz4_crc(literal, sizeof(literal)));
    if (!zeal_lz4_block_decode(literal, sizeof(literal), out, 5) ||
        memcmp(out, "hello", 5) ||
        !zeal_lz4_block_decode(repeat, sizeof(repeat), out, 7) ||
        memcmp(out, "aaaaaa!", 7) ||
        zeal_lz4_block_decode(literal, sizeof(literal), out, 4) ||
        zeal_lz4_block_decode(bad_offset, sizeof(bad_offset), out, 5) ||
        zeal_lz4_block_decode(truncated, sizeof(truncated), out, sizeof(out)) ||
        !zeal_lz4_archive_decode(archive, sizeof(archive), out, 5)) {
        fputs("FAIL LZ4 block decode\n", stderr);
        return 1;
    }
    archive[29] ^= 1;
    if (zeal_lz4_archive_decode(archive, sizeof(archive), out, 5)) {
        fputs("FAIL damaged LZ4 archive accepted\n", stderr);
        return 1;
    }
    puts("PASS LZ4 block decoding and malformed input rejection");
    return 0;
}
