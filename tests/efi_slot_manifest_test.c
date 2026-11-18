#include <stdio.h>
#include <string.h>
#include "../src/efi_slot_manifest.h"
struct disk { uint8_t sectors[2100][512]; };
static int rd(void *ctx, uint64_t lba, uint8_t b[512]) {
    struct disk *d = ctx; if (lba >= 2100) return 0; memcpy(b, d->sectors[lba], 512); return 1;
}
static int wr(void *ctx, uint64_t lba, const uint8_t b[512]) {
    struct disk *d = ctx; if (lba >= 2100) return 0; memcpy(d->sectors[lba], b, 512); return 1;
}
static int flush(void *ctx) { (void)ctx; return 1; }
int main(void) {
    static struct disk d;
    struct zu_journal j = {{&d, rd, wr, flush, 2100, 34}, 2048, 2100, ZU_JOURNAL_MAX_SLOTS, 0};
    struct zu_slot_fingerprint a = {{1, 2, 3}, {4, 5, 6}}, b;
    for (unsigned i = 0; i < 16; i++) d.sectors[1][56+i] = (uint8_t)(i+1);
    if (zu_slot_manifest_read(&j, 0, &b) != 0 || !zu_slot_manifest_write(&j, 0, &a) ||
        zu_slot_manifest_read(&j, 0, &b) != 1 || memcmp(&a, &b, sizeof(a))) return 1;
    d.sectors[ZU_SLOT_U_SHADOW][40] ^= 1;
    if (zu_slot_manifest_read(&j, 0, &b) != -1) return 1;
    if (!zu_slot_manifest_write(&j, 0, &a)) return 1;
    d.sectors[1][56] ^= 1;
    if (zu_slot_manifest_read(&j, 0, &b) != -1) return 1;
    puts("PASS slot fingerprints, damaged copies and GPT binding"); return 0;
}
