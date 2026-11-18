#include <stdio.h>
#include <stdint.h>
#include "../src/efi_fat32_read.h"
#include "../src/lz4_stream.h"
struct disk { FILE *f; };
static int read_sector(void *ctx, uint64_t lba, uint8_t b[512]) {
    struct disk *d = ctx;
    return !fseeko(d->f, (off_t)(lba * 512), SEEK_SET) && fread(b, 1, 512, d->f) == 512;
}
struct bytes { struct efi_source_reader reader; uint8_t sector[512]; uint32_t offset, left; };
static int read_byte(void *ctx, uint8_t *out) {
    struct bytes *b = ctx;
    if (!b->left) return 0;
    if (b->offset == 512) {
        if (!efi_source_read_next(&b->reader, b->sector)) return 0;
        b->offset = 0;
    }
    *out = b->sector[b->offset++]; b->left--; return 1;
}
int main(int argc, char **argv) {
    static struct zeal_lz4_stream stream;
    static struct bytes bytes;
    if (argc != 3) return 2;
    struct disk d = {fopen(argv[1], "rb")}; FILE *expected = fopen(argv[2], "rb");
    if (!d.f || !expected || !efi_source_open_compressed(read_sector, &d, 2048, 128863, &bytes.reader)) return 1;
    bytes.offset = 512; bytes.left = bytes.reader.bytes;
    if (!zeal_lz4_stream_open(&stream, read_byte, &bytes, bytes.reader.bytes)) return 1;
    int c; uint8_t actual;
    while ((c = fgetc(expected)) != EOF)
        if (zeal_lz4_stream_byte(&stream, &actual) != 1 || actual != (uint8_t)c) return 1;
    if (zeal_lz4_stream_byte(&stream, &actual) != 0 || bytes.left) return 1;
    puts("PASS FAT32 compressed baseline streaming");
    fclose(d.f); fclose(expected); return 0;
}
