/* Bounded streaming decoder for the checksummed ZEALLZ41 raw LZ4 archive.
 * The caller supplies exactly the archive bytes. At most 64 KiB of history is
 * retained, independent of the uncompressed archive size. */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "block_partition.h"

typedef int (*zeal_stream_read_fn)(void *, uint8_t *);
struct zeal_lz4_stream {
    zeal_stream_read_fn read; void *ctx;
    uint8_t history[65536], token;
    uint32_t input_size, input_pos, output_size, output_pos;
    uint32_t input_crc, output_crc, expected_input_crc, expected_output_crc;
    uint32_t literals, match, distance;
    int state, failed;
};
static uint32_t zeal_stream_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static int zeal_stream_input(struct zeal_lz4_stream *s, uint8_t *v) {
    if (s->input_pos >= s->input_size || !s->read(s->ctx, v)) return 0;
    s->input_pos++; s->input_crc = zeal_partition_crc_step(s->input_crc, *v); return 1;
}
static int zeal_lz4_stream_open(struct zeal_lz4_stream *s, zeal_stream_read_fn read,
                                void *ctx, uint32_t bytes) {
    uint8_t h[24]; const char *magic = "ZEALLZ41";
    if (!s || !read || bytes < 24) return 0;
    for (unsigned i = 0; i < 24; i++) if (!read(ctx, h + i)) return 0;
    for (unsigned i = 0; i < 8; i++) if (h[i] != (uint8_t)magic[i]) return 0;
    uint32_t size = zeal_stream_u32(h + 8), packed = zeal_stream_u32(h + 12);
    if (!size || size > 128u*1024u*1024u || size % 512 || packed != bytes - 24) return 0;
    s->read = read; s->ctx = ctx; s->input_size = packed; s->input_pos = 0;
    s->output_size = size; s->output_pos = 0; s->input_crc = s->output_crc = 0xffffffffu;
    s->expected_output_crc = zeal_stream_u32(h + 16); s->expected_input_crc = zeal_stream_u32(h + 20);
    s->literals = s->match = s->distance = 0; s->state = 0; s->failed = 0; return 1;
}
static int zeal_stream_length(struct zeal_lz4_stream *s, uint32_t *length) {
    uint8_t b;
    do {
        if (!zeal_stream_input(s, &b) || *length > UINT32_MAX - b) return 0;
        *length += b;
    } while (b == 255);
    return 1;
}
/* 1=byte, 0=verified end, -1=corruption or truncated input. */
static int zeal_lz4_stream_byte(struct zeal_lz4_stream *s, uint8_t *out) {
    uint8_t b, lo, hi;
    if (!s || !out || s->failed) return -1;
    for (;;) {
        if (s->state == 0) {
            if (s->input_pos == s->input_size) {
                if (s->output_pos == s->output_size &&
                    (s->input_crc ^ 0xffffffffu) == s->expected_input_crc &&
                    (s->output_crc ^ 0xffffffffu) == s->expected_output_crc) return 0;
                break;
            }
            if (!zeal_stream_input(s, &s->token)) break;
            s->literals = s->token >> 4;
            if (s->literals == 15 && !zeal_stream_length(s, &s->literals)) break;
            if (s->literals > s->output_size - s->output_pos) break;
            s->state = 1;
        }
        if (s->state == 1) {
            if (s->literals) {
                if (!zeal_stream_input(s, &b)) break;
                s->literals--; goto emit;
            }
            if (s->input_pos == s->input_size) { s->state = 0; continue; }
            if (!zeal_stream_input(s, &lo) || !zeal_stream_input(s, &hi)) break;
            s->distance = (uint32_t)lo | ((uint32_t)hi << 8);
            if (!s->distance || s->distance > s->output_pos) break;
            s->match = s->token & 15u;
            if (s->match == 15 && !zeal_stream_length(s, &s->match)) break;
            if (s->match > UINT32_MAX - 4) break;
            s->match += 4;
            if (s->match > s->output_size - s->output_pos) break;
            s->state = 2;
        }
        if (s->state == 2) {
            if (!s->match) { s->state = 0; continue; }
            b = s->history[(s->output_pos - s->distance) & 65535u]; s->match--; goto emit;
        }
    }
    s->failed = 1; return -1;
emit:
    if (s->output_pos >= s->output_size) { s->failed = 1; return -1; }
    s->history[s->output_pos & 65535u] = b; s->output_pos++;
    s->output_crc = zeal_partition_crc_step(s->output_crc, b); *out = b; return 1;
}
