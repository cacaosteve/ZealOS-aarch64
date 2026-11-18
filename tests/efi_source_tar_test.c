#include <stdio.h>
#include <stdint.h>
#include <sys/types.h>
#include "../src/efi_source_tar.h"
struct input { FILE *file; uint64_t corrupt; };
static int rd(void *ctx, uint64_t lba, uint8_t out[512]) {
    struct input *in = ctx;
    if (fseeko(in->file, (off_t)(lba * 512), SEEK_SET) || fread(out, 1, 512, in->file) != 512) return 0;
    if (lba == in->corrupt) out[0] ^= 1;
    return 1;
}
int main(int argc, char **argv) {
    struct efi_source_reader reader;
    struct efi_source_tar_entry entry;
    uint8_t bytes[512];
    if (argc != 2) return 2;
    struct input in = {fopen(argv[1], "rb"), UINT64_MAX};
    if (!in.file || !efi_source_open(rd, &in, 2048, 128863, &reader)) return 1;
    uint64_t first_lba = reader.space.data_first + (uint64_t)(reader.first_cluster - 2) * reader.space.sectors_per_cluster;
    struct efi_source_tar tar = {.reader = &reader};
    int status;
    while ((status = efi_source_tar_next(&tar, &entry)) == 1) {
        uint32_t crc = 0xffffffffu;
        while (tar.remaining) {
            uint32_t used;
            if (!efi_source_tar_data(&tar, bytes, &used)) return 1;
            for (uint32_t i = 0; i < used; i++) crc = zeal_partition_crc_step(crc, bytes[i]);
        }
        printf("%s\t%u\t%u\n", entry.name, entry.bytes, crc ^ 0xffffffffu);
    }
    if (status != 0) return 1;
    in.corrupt = first_lba;
    if (!efi_source_open(rd, &in, 2048, 128863, &reader)) return 1;
    tar = (struct efi_source_tar){.reader = &reader};
    if (efi_source_tar_next(&tar, &entry) != -1) return 1;
    fclose(in.file);
    return 0;
}
