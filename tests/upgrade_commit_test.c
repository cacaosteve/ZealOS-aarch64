#include <stdio.h>
#include <string.h>
#include "../src/upgrade_commit.h"

struct disk { uint8_t sector[8][512]; int calls, fail_at, tear; };
static int interrupted(struct disk *d) { return ++d->calls == d->fail_at; }
static int rd(void *p, uint64_t lba, uint8_t out[512]) {
    struct disk *d = p;
    if (lba >= 8 || interrupted(d)) return 0;
    memcpy(out, d->sector[lba], 512); return 1;
}
static int wr(void *p, uint64_t lba, const uint8_t in[512]) {
    struct disk *d = p;
    if (lba >= 8) return 0;
    if (interrupted(d)) {
        if (d->tear && lba == 2) memcpy(d->sector[lba], in, 256);
        return 0;
    }
    memcpy(d->sector[lba], in, 512); return 1;
}
static int flush(void *p) { return !interrupted(p); }

int main(void) {
    uint8_t replacement[512]; memset(replacement, 0x7b, sizeof(replacement));
    for (int fail = 1; fail < 30; fail++) {
        struct disk d = {0}; memset(d.sector[2], 0x39, 512);
        d.fail_at = fail; d.tear = 1;
        struct zu_commit_io io = {&d, rd, wr, flush, 8, 5};
        int success = zu_commit(&io, 2, replacement);
        d.fail_at = 0;
        if (!zu_recover(&io)) { fprintf(stderr, "recovery failed at %d\n", fail); return 1; }
        uint8_t wanted = success ? 0x7b : 0x39;
        /* A failure clearing the marker can leave an already-committed sector
         * with a clean journal; either fully old or fully new is acceptable. */
        if (!success && d.sector[2][0] == 0x7b) wanted = 0x7b;
        for (unsigned i = 0; i < 512; i++)
            if (d.sector[2][i] != wanted) { fprintf(stderr, "mixed data at %d\n", fail); return 1; }
        if (!zu_empty(d.sector[5])) return 1;
    }
    /* Never apply recovery from a damaged or forged journal. */
    {
        struct disk d = {0};
        struct zu_commit_io io = {&d, rd, wr, flush, 8, 5};
        memset(d.sector[2], 0x39, 512);
        memcpy(d.sector[5], "ZUCOMM01", 8);
        zu_put64(d.sector[5] + 8, 2);
        zu_put32(d.sector[5] + 16, zeal_partition_crc(d.sector[6], 512));
        zu_put32(d.sector[5] + 24, zeal_partition_crc(d.sector[5], 24));
        d.sector[6][0] = 1; /* saved original fails its checksum */
        if (zu_recover(&io) || d.sector[2][0] != 0x39) return 1;
        d.sector[6][0] = 0;
        d.sector[5][24] ^= 1; /* marker fails its checksum */
        if (zu_recover(&io) || d.sector[2][0] != 0x39) return 1;
    }
    puts("PASS commit interruption and torn target recovery");
    return 0;
}
