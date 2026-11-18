#include <stdio.h>
#include <stdint.h>
#include <sys/types.h>
#include <string.h>
#include "../src/efi_source_tar.h"
struct input { FILE *file; uint64_t corrupt; };
static int rd(void *ctx, uint64_t lba, uint8_t out[512]) {
    struct input *in = ctx;
    if (fseeko(in->file, (off_t)(lba * 512), SEEK_SET) || fread(out, 1, 512, in->file) != 512) return 0;
    if (lba == in->corrupt) out[100] ^= 1;
    return 1;
}
int main(int argc, char **argv) {
    static struct efi_compressed_source source;
    struct efi_source_tar tar; struct efi_source_tar_entry entry; uint8_t bytes[512];
    if (argc != 2 && argc != 3) return 2;
    struct input in = {fopen(argv[1], "rb"), UINT64_MAX};
    int slot_v = argc == 3 && !strcmp(argv[2], "v");
    if (!in.file || !efi_compressed_source_open_slot(rd, &in, 2048, 128863, &source, &tar, slot_v)) return 1;
    if (argc == 3 && !strcmp(argv[2], "corrupt")) {
        in.corrupt = source.reader.space.data_first +
            (uint64_t)(source.reader.first_cluster - 2) * source.reader.space.sectors_per_cluster + 4;
        if (!efi_compressed_source_open(rd, &in, 2048, 128863, &source, &tar)) return 1;
    }
    int status;
    while ((status = efi_source_tar_next(&tar, &entry)) == 1) {
        uint32_t crc = 0xffffffffu;
        while (tar.remaining) {
            uint32_t used;
            if (!efi_source_tar_data(&tar, bytes, &used)) return argc == 3 && !slot_v ? 0 : 1;
            for (uint32_t i = 0; i < used; i++) crc = zeal_partition_crc_step(crc, bytes[i]);
        }
        printf("%s\t%u\t%u\n", entry.name, entry.bytes, crc ^ 0xffffffffu);
    }
    fclose(in.file);
    return argc == 3 && !slot_v ? (status == -1 ? 0 : 1) : (status == 0 ? 0 : 1);
}
