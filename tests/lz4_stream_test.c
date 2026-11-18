#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "../src/lz4_stream.h"
struct input { const uint8_t *bytes; size_t size, pos; };
static int read_byte(void *ctx, uint8_t *out) {
    struct input *in = ctx;
    if (in->pos == in->size) return 0;
    *out = in->bytes[in->pos++]; return 1;
}
static uint8_t *load(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f || fseek(f, 0, SEEK_END)) return NULL;
    long n = ftell(f); if (n < 0 || fseek(f, 0, SEEK_SET)) return NULL;
    uint8_t *p = malloc((size_t)n); if (!p || fread(p, 1, (size_t)n, f) != (size_t)n) return NULL;
    fclose(f); *size = (size_t)n; return p;
}
int main(int argc, char **argv) {
    static struct zeal_lz4_stream stream;
    size_t packed_size, plain_size; uint8_t byte;
    if (argc != 3) return 2;
    uint8_t *packed = load(argv[1], &packed_size), *plain = load(argv[2], &plain_size);
    if (!packed || !plain || packed_size > UINT32_MAX) return 1;
    struct input in = {packed, packed_size, 0};
    if (!zeal_lz4_stream_open(&stream, read_byte, &in, (uint32_t)packed_size)) return 1;
    for (size_t i = 0; i < plain_size; i++)
        if (zeal_lz4_stream_byte(&stream, &byte) != 1 || byte != plain[i]) return 1;
    if (zeal_lz4_stream_byte(&stream, &byte) != 0) return 1;
    /* Damage the archived output checksum: no output may be accepted as a
     * verified baseline even when decompression itself is otherwise valid. */
    packed[16] ^= 1; in.pos = 0;
    if (!zeal_lz4_stream_open(&stream, read_byte, &in, (uint32_t)packed_size)) return 1;
    for (size_t i = 0; i < plain_size; i++)
        if (zeal_lz4_stream_byte(&stream, &byte) != 1) return 1;
    if (zeal_lz4_stream_byte(&stream, &byte) != -1) return 1;
    puts("PASS streaming LZ4 baseline and checksum rejection");
    free(packed); free(plain); return 0;
}
