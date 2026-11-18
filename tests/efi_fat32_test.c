#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include "../src/efi_fat32.h"

struct fixture { FILE *file; uint64_t corrupt_lba; };
static int read_sector(void *context, uint64_t sector, uint8_t out[512]) {
    struct fixture *f = context;
    if (fseeko(f->file, (off_t)(sector * 512), SEEK_SET) ||
        fread(out, 1, 512, f->file) != 512) return 0;
    if (sector == f->corrupt_lba) out[8] ^= 1;
    return 1;
}
static int test(const char *path) {
    struct fixture f = {fopen(path, "rb"), UINT64_MAX};
    struct efi_fat32_space space;
    if (!f.file || !efi_fat32_scan_space(read_sector, &f, 2048, 128863, &space) ||
        !space.free_clusters || !space.largest_run) return 1;
    printf("PASS %s: %u free clusters, largest run %u\n",
           path, space.free_clusters, space.largest_run);
    f.corrupt_lba = space.fat_first + space.fat_sectors;
    if (efi_fat32_scan_space(read_sector, &f, 2048, 128863, &space)) return 1;
    fclose(f.file);
    return 0;
}
int main(int argc, char **argv) {
    if (argc != 3) return 2;
    return test(argv[1]) || test(argv[2]);
}
