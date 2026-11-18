#include <stdio.h>
#include <stdint.h>
#include <sys/types.h>
#include "../src/efi_fat32_reclaim_plan.h"
struct disk { FILE *f; };
static int rd(void *ctx, uint64_t lba, uint8_t b[512]) {
    struct disk *d = ctx;
    return !fseeko(d->f, (off_t)(lba * 512), SEEK_SET) && fread(b, 1, 512, d->f) == 512;
}
int main(int argc, char **argv) {
    static struct efi_fat32_ownership owner;
    if (argc != 3) return 2;
    struct disk d = {fopen(argv[1], "rb")}; if (!d.f) return 1;
    int valid = efi_fat32_ownership_check(rd, &d, 2048, 128863, &owner);
    if (valid) {
        struct efi_reclaim_plan plan;
        if (!efi_fat32_reclaim_plan(rd, &d, 2048, 128863, &owner, &plan) ||
            !plan.clusters || plan.metadata_records != 2 * plan.fat_sectors + 1) return 1;
        printf("archive: %u clusters, %u mirrored metadata records\n", plan.clusters, plan.metadata_records);
    }
    fclose(d.f);
    if (valid != (argv[2][0] == '1')) return 1;
    printf("PASS FAT ownership %s\n", valid ? "valid" : "rejected"); return 0;
}
