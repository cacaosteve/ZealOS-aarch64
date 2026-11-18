#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/efi_upgrade_owner.h"
struct disk { uint8_t data[2100][512]; uint64_t tear_lba; unsigned tear_bytes; };
static int rd(void *ctx, uint64_t lba, uint8_t b[512]) {
    struct disk *d = ctx; if (lba >= 2100) return 0;
    memcpy(b, d->data[lba], 512); return 1;
}
static int wr(void *ctx, uint64_t lba, const uint8_t b[512]) {
    struct disk *d = ctx; if (lba >= 2100) return 0;
    if (lba == d->tear_lba && d->tear_bytes) { memcpy(d->data[lba], b, d->tear_bytes); return 0; }
    memcpy(d->data[lba], b, 512); return 1;
}
static int flush(void *ctx) { (void)ctx; return 1; }
int main(void) {
    static struct disk d;
    struct zu_journal j = {{&d, rd, wr, flush, 2100, 34}, 2048, 2100, ZU_JOURNAL_MAX_SLOTS, 0};
    for (unsigned i = 0; i < 16; i++) d.data[1][56+i] = (uint8_t)(i+1);
    if (zu_owner_prepare(&j, 1) || !zu_journal_begin(&j) || !zu_journal_commit(&j)) return 1;
    d.data[2000][0] = 1;
    if (zu_owner_prepare(&j, 1)) return 1;
    d.data[2000][0] = 0;
    const unsigned tears[] = {1, 7, 16, 24, 28, 511};
    for (unsigned copy = 0; copy < 2; copy++) for (unsigned k = 0; k < sizeof(tears)/sizeof(tears[0]); k++) {
        memset(d.data[ZU_OWNER_LBA], 0, 512); memset(d.data[ZU_OWNER_SHADOW_LBA], 0, 512);
        d.tear_lba = copy ? ZU_OWNER_LBA : ZU_OWNER_SHADOW_LBA; d.tear_bytes = tears[k];
        if (zu_owner_prepare(&j, 1)) return 1;
        d.tear_bytes = 0;
        if (!zu_owner_prepare(&j, 1)) return 1;
    }
    memset(d.data[ZU_OWNER_LBA], 0, 512); memset(d.data[ZU_OWNER_SHADOW_LBA], 0, 512);
    d.data[ZU_OWNER_SHADOW_LBA][40] = 1;
    if (zu_owner_prepare(&j, 1)) return 1;
    memset(d.data[ZU_OWNER_SHADOW_LBA], 0, 512);
    if (!zu_owner_prepare(&j, 1)) return 1;
    d.tear_lba = j.io.journal + 1 + 3ull*j.slots; d.tear_bytes = 7;
    if (zu_owner_clear_commit(&j)) return 1;
    d.tear_bytes = 0;
    if (!zu_owner_prepare(&j, 0) || !zu_owner_clear_commit(&j) || zu_journal_committed(&j) != 0 ||
        !zu_owner_prepare(&j, 0)) return 1;
    memcpy(d.data[ZU_PENDING_SHADOW], "ZUPEND0", 7);
    if (!zu_owner_recover_torn_pending(&j) || !zu_empty(d.data[ZU_PENDING_SHADOW])) return 1;
    d.data[ZU_PENDING_SHADOW][0] = 'X';
    if (zu_owner_recover_torn_pending(&j)) return 1;
    memset(d.data[ZU_PENDING_SHADOW], 0, 512);
    d.data[1][56] ^= 1;
    if (zu_owner_prepare(&j, 0)) return 1;
    puts("PASS journal ownership, torn claim, stale commit and disk binding"); return 0;
}
