#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "../src/upgrade_preflight.h"

struct fixture { FILE *file; uint64_t sectors; uint64_t corrupt; };
static int synthetic_v2;
static uint64_t synthetic_bank;
static int fixture_read(void *context, uint64_t lba, uint8_t out[512]) {
    struct fixture *f = context;
    if (lba >= f->sectors || fseeko(f->file, (off_t)(lba * 512), SEEK_SET) ||
        fread(out, 1, 512, f->file) != 512) return 0;
    if (lba == f->corrupt) out[0] ^= 0xff;
    if (synthetic_v2 && lba == synthetic_bank) {
        memset(out, 0, 512);
        memcpy(out, "ZCSRC002", 8);
        out[8] = 2;
        out[14] = 1; /* 65536 sectors */
        out[16] = synthetic_v2 == 2 ? 0 : 1;
        uint32_t crc = zeal_partition_crc(out, 20);
        for (unsigned i = 0; i < 4; i++) out[20+i] = (uint8_t)(crc >> (8*i));
        if (synthetic_v2 == 3) out[20] ^= 1;
    }
    return 1;
}
static int check(const char *name, int legacy) {
    struct fixture f = {0};
    struct zeal_upgrade_target target = {0};
    f.file = fopen(name, "rb");
    if (!f.file || fseeko(f.file, 0, SEEK_END)) return 1;
    off_t size = ftello(f.file);
    if (size <= 0 || size % 512) return 1;
    f.sectors = (uint64_t)size / 512;
    f.corrupt = UINT64_MAX;
    if (!zeal_upgrade_gap_empty(fixture_read, &f)) return 1;
    f.corrupt = 34;
    if (zeal_upgrade_gap_empty(fixture_read, &f)) return 1;
    f.corrupt = UINT64_MAX;
    if (!zeal_upgrade_preflight(fixture_read, &f, f.sectors, &target) ||
        target.legacy != legacy || target.esp_first != 2048) return 1;
    synthetic_bank = target.bank_a;
    synthetic_v2 = 1;
    if (!zeal_upgrade_preflight(fixture_read, &f, f.sectors, &target)) return 1;
    synthetic_v2 = 2;
    if (zeal_upgrade_preflight(fixture_read, &f, f.sectors, &target)) return 1;
    synthetic_v2 = 3;
    if (zeal_upgrade_preflight(fixture_read, &f, f.sectors, &target)) return 1;
    synthetic_v2 = 0;
    f.corrupt = 1;
    if (zeal_upgrade_preflight(fixture_read, &f, f.sectors, &target)) return 1;
    f.corrupt = 2;
    if (zeal_upgrade_preflight(fixture_read, &f, f.sectors, &target)) return 1;
    f.corrupt = f.sectors - 1;
    if (zeal_upgrade_preflight(fixture_read, &f, f.sectors, &target)) return 1;
    f.corrupt = f.sectors - 33;
    if (zeal_upgrade_preflight(fixture_read, &f, f.sectors, &target)) return 1;
    f.corrupt = target.esp_first;
    if (zeal_upgrade_preflight(fixture_read, &f, f.sectors, &target)) return 1;
    /* These fixtures have only the primary source bank initialized. */
    f.corrupt = target.bank_a;
    if (zeal_upgrade_preflight(fixture_read, &f, f.sectors, &target)) return 1;
    fclose(f.file);
    printf("PASS upgrade preflight: %s (%s), corrupted GPT, EFI and bank rejection\n", name,
           legacy ? "legacy" : "expanded");
    return 0;
}
int main(int argc, char **argv) {
    if (argc != 3) { fprintf(stderr, "usage: %s LEGACY EXPANDED\n", argv[0]); return 2; }
    return check(argv[1], 1) || check(argv[2], 0);
}
