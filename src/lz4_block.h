/* Bounds-checked LZ4 block decompression. This is a raw block decoder, not
 * an LZ4 frame parser; the caller must authenticate the block and its size. */
#pragma once
#include <stddef.h>
#include <stdint.h>

static int zeal_lz4_block_decode(const uint8_t *src, size_t src_len,
                                  uint8_t *dst, size_t dst_len) {
    size_t in = 0, out = 0;
    if ((!src && src_len) || (!dst && dst_len)) return 0;
    while (in < src_len) {
        uint8_t token = src[in++];
        size_t literals = token >> 4, match = token & 15u;
        if (literals == 15) {
            uint8_t n;
            do {
                if (in >= src_len) return 0;
                n = src[in++];
                if (literals > SIZE_MAX - n) return 0;
                literals += n;
            } while (n == 255);
        }
        if (literals > src_len - in || literals > dst_len - out) return 0;
        for (size_t i = 0; i < literals; i++) dst[out++] = src[in++];
        if (in == src_len) return out == dst_len;
        if (src_len - in < 2) return 0;
        size_t distance = (size_t)src[in] | ((size_t)src[in + 1] << 8);
        in += 2;
        if (!distance || distance > out) return 0;
        if (match == 15) {
            uint8_t n;
            do {
                if (in >= src_len) return 0;
                n = src[in++];
                if (match > SIZE_MAX - n) return 0;
                match += n;
            } while (n == 255);
        }
        if (match > SIZE_MAX - 4) return 0;
        match += 4;
        if (match > dst_len - out) return 0;
        for (size_t i = 0; i < match; i++) {
            dst[out] = dst[out - distance];
            out++;
        }
    }
    return out == dst_len;
}
