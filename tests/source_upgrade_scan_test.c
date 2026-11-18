#include <stdio.h>
#include <stdint.h>
#include <sys/types.h>
#include "../src/upgrade_preflight.h"
#include "../src/source_upgrade_scan.h"
struct input { FILE *file; uint64_t corrupt; };
static int rd(void *ctx, uint64_t lba, uint8_t out[512]) {
    struct input *in = ctx;
    if (fseeko(in->file, (off_t)(lba * 512), SEEK_SET) || fread(out, 1, 512, in->file) != 512) return 0;
    if (lba == in->corrupt) out[0] ^= 1;
    return 1;
}
int main(int argc, char **argv) {
    static struct zu_source_scan scan;
    struct zeal_upgrade_target target;
    struct efi_source_reader reader;
    struct zu_source_stats stats;
    if (argc != 2) return 2;
    struct input in = {fopen(argv[1], "rb"), UINT64_MAX};
    if (!in.file || fseeko(in.file, 0, SEEK_END)) return 1;
    off_t bytes = ftello(in.file);
    if (bytes <= 0 || bytes % 512 ||
        !zeal_upgrade_preflight(rd, &in, (uint64_t)bytes / 512, &target) ||
        !zu_source_catalog(rd, &in, target.bank_a, target.bank_b, &scan) ||
        !efi_source_open(rd, &in, target.esp_first, target.esp_sectors, &reader)) return 1;
    struct efi_source_tar tar = {.reader = &reader};
    if (!zu_source_baseline_compare(&scan, &tar, &stats)) return 1;
    printf("bank %llu generation %u files %u unchanged %u edited %u deleted %u missing %u user-only %u\n",
           (unsigned long long)scan.bank, scan.generation, scan.count,
           stats.unchanged, stats.edited, stats.deleted, stats.missing, stats.user_only);
    /* A changed payload in a live record must be rejected by catalog CRC. */
    for (uint32_t i = 0; i < scan.count; i++) if (!scan.items[i].deleted && scan.items[i].bytes) {
        in.corrupt = scan.bank + scan.items[i].header + 1;
        if (zu_source_catalog(rd, &in, target.bank_a, target.bank_b, &scan)) return 1;
        break;
    }
    fclose(in.file);
    return 0;
}
