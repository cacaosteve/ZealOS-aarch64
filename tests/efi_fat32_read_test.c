#include <stdio.h>
#include <stdint.h>
#include <sys/types.h>
#include "../src/efi_fat32_read.h"
struct input { FILE *file; uint64_t corrupt, fat_first; uint32_t fat_sectors, loop_cluster; };
static int rd(void *ctx, uint64_t lba, uint8_t out[512]) {
    struct input *in = ctx;
    if (fseeko(in->file, (off_t)(lba * 512), SEEK_SET) || fread(out, 1, 512, in->file) != 512) return 0;
    if (lba == in->corrupt) out[8] ^= 1;
    if (in->loop_cluster) {
        uint64_t index = in->loop_cluster / 128;
        if (lba == in->fat_first + index || lba == in->fat_first + in->fat_sectors + index) {
            uint32_t offset = (in->loop_cluster % 128) * 4;
            for (unsigned i = 0; i < 4; i++) out[offset + i] = (uint8_t)(in->loop_cluster >> (8*i));
        }
    }
    return 1;
}
int main(int argc, char **argv) {
    struct efi_source_reader r;
    uint8_t data[512];
    if (argc != 3) return 2;
    struct input in = {fopen(argv[1], "rb"), UINT64_MAX, 0, 0, 0};
    FILE *out = fopen(argv[2], "wb");
    if (!in.file || !out || !efi_source_open(rd, &in, 2048, 128863, &r)) return 1;
    uint32_t sectors = r.sectors_left;
    for (uint32_t i = 0; i < sectors; i++)
        if (!efi_source_read_next(&r, data) || fwrite(data, 1, 512, out) != 512) return 1;
    if (efi_source_read_next(&r, data) || fclose(out)) return 1;
    in.fat_first = r.space.fat_first; in.fat_sectors = r.space.fat_sectors;
    in.loop_cluster = r.first_cluster;
    if (efi_source_open(rd, &in, 2048, 128863, &r)) return 1;
    in.loop_cluster = 0;
    in.corrupt = 2080; /* first FAT: mismatch with the second copy */
    if (efi_source_open(rd, &in, 2048, 128863, &r)) return 1;
    fclose(in.file);
    printf("PASS streamed %u sectors; rejected FAT loop and mirror mismatch\n", sectors);
    return 0;
}
