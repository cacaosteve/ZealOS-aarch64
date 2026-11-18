#include <stdio.h>
#include <string.h>
#include "../src/upgrade_journal.h"
struct test_disk { uint8_t data[32][512]; unsigned calls, fail_at; int tear_marker, tear_any; };
static int fail(struct test_disk *d) { return ++d->calls == d->fail_at; }
static int rd(void *p, uint64_t lba, uint8_t out[512]) {
    struct test_disk *d = p;
    if (lba >= 32 || fail(d)) return 0;
    memcpy(out, d->data[lba], 512); return 1;
}
static int wr(void *p, uint64_t lba, const uint8_t in[512]) {
    struct test_disk *d = p;
    if (lba >= 32) return 0;
    if (fail(d)) {
        if (d->tear_any) memcpy(d->data[lba], in, (size_t)d->tear_any);
        if (d->tear_marker && lba == 14) memcpy(d->data[lba], in, 7);
        return 0;
    }
    memcpy(d->data[lba], in, 512); return 1;
}
static int sync_disk(void *p) { return !fail(p); }
struct large_disk { uint8_t data[2200][512]; };
static int large_read(void *p, uint64_t lba, uint8_t out[512]) {
    struct large_disk *d = p;
    if (lba >= 2200) return 0;
    memcpy(out, d->data[lba], 512); return 1;
}
static int large_write(void *p, uint64_t lba, const uint8_t in[512]) {
    struct large_disk *d = p;
    if (lba >= 2200) return 0;
    memcpy(d->data[lba], in, 512); return 1;
}
static int large_flush(void *p) { (void)p; return 1; }
int main(void) {
    uint8_t first[512], second[512];
    memset(first, 0x51, sizeof(first)); memset(second, 0x63, sizeof(second));
    {
        struct test_disk d = {0};
        struct zu_journal j = {{&d, rd, wr, sync_disk, 32, 1}, 20, 25, 4, 0};
        if (zu_journal_committed(&j) != 0 || !zu_journal_begin(&j) ||
            zu_journal_committed(&j) != 0 || !zu_journal_write(&j, 20, first) ||
            !zu_journal_commit(&j) || zu_journal_committed(&j) != 1) return 1;
        d.data[14][12] ^= 1;
        if (zu_journal_committed(&j) != 0) return 1;
    }
    const int tear_sizes[] = {1, 7, 16, 31, 256, 511};
    for (unsigned t = 0; t < sizeof(tear_sizes) / sizeof(tear_sizes[0]); t++)
    for (unsigned failure = 1; failure <= 180; failure++) {
        struct test_disk d = {0};
        memset(d.data[20], 0x11, 512); memset(d.data[21], 0x22, 512);
        d.fail_at = failure; d.tear_any = tear_sizes[t];
        struct zu_journal j = {{&d, rd, wr, sync_disk, 32, 1}, 20, 25, 4, 0};
        if (zu_journal_begin(&j) && zu_journal_write(&j, 20, first) &&
            zu_journal_write(&j, 21, second)) (void)zu_journal_write(&j, 20, second);
        d.fail_at = 0; d.tear_any = 0;
        if (!zu_journal_recover(&j)) { fprintf(stderr, "recovery failed at %u\n", failure); return 1; }
        for (unsigned i = 0; i < 512; i++)
            if (d.data[20][i] != 0x11 || d.data[21][i] != 0x22) {
                fprintf(stderr, "wrong data at %u\n", failure); return 1;
            }
    }
    for (unsigned failure = 1; failure <= 16; failure++) {
        struct test_disk d = {0};
        memset(d.data[20], 0x11, 512); memset(d.data[21], 0x22, 512);
        struct zu_journal j = {{&d, rd, wr, sync_disk, 32, 1}, 20, 25, 4, 0};
        if (!zu_journal_begin(&j) || !zu_journal_write(&j, 20, first) ||
            !zu_journal_write(&j, 21, second)) return 1;
        d.fail_at = d.calls + failure; d.tear_marker = 1;
        (void)zu_journal_commit(&j);
        d.fail_at = 0;
        if (!zu_journal_recover(&j)) { fprintf(stderr, "commit recovery failed at %u\n", failure); return 1; }
        int old = d.data[20][0] == 0x11 && d.data[21][0] == 0x22;
        int new = d.data[20][0] == 0x51 && d.data[21][0] == 0x63;
        if (!old && !new) return 1;
        for (unsigned i = 0; i < 512; i++)
            if (d.data[20][i] != (old ? 0x11 : 0x51) ||
                d.data[21][i] != (old ? 0x22 : 0x63)) return 1;
    }
    for (unsigned damaged = 0; damaged < 2; damaged++) {
        struct test_disk d = {0};
        memset(d.data[20], 0x11, 512);
        struct zu_journal j = {{&d, rd, wr, sync_disk, 32, 1}, 20, 25, 4, 0};
        if (!zu_journal_begin(&j) || !zu_journal_write(&j, 20, first)) return 1;
        d.data[damaged ? 15 : 1][0] ^= 0xff;
        if (!zu_journal_recover(&j)) return 1;
        for (unsigned i = 0; i < 512; i++) if (d.data[20][i] != 0x11) return 1;
    }
    for (unsigned t = 0; t < sizeof(tear_sizes) / sizeof(tear_sizes[0]); t++)
    for (unsigned failure = 1; failure <= 100; failure++) {
        struct test_disk d = {0};
        memset(d.data[20], 0x11, 512); memset(d.data[21], 0x22, 512);
        struct zu_journal j = {{&d, rd, wr, sync_disk, 32, 1}, 20, 25, 4, 0};
        if (!zu_journal_begin(&j) || !zu_journal_write(&j, 20, first) ||
            !zu_journal_write(&j, 21, second)) return 1;
        d.fail_at = d.calls + failure; d.tear_any = tear_sizes[t];
        (void)zu_journal_recover(&j);
        d.fail_at = 0; d.tear_any = 0;
        if (!zu_journal_recover(&j)) { fprintf(stderr, "retry failed at %u\n", failure); return 1; }
        for (unsigned i = 0; i < 512; i++)
            if (d.data[20][i] != 0x11 || d.data[21][i] != 0x22) return 1;
    }
    for (unsigned damaged = 0; damaged < 2; damaged++) {
        struct test_disk d = {0}; memset(d.data[20], 0x11, 512);
        struct zu_journal j = {{&d, rd, wr, sync_disk, 32, 1}, 20, 25, 4, 0};
        if (!zu_journal_begin(&j) || !zu_journal_write(&j, 20, first)) return 1;
        d.data[damaged ? 3 : 2][0] ^= 0xff;
        if (!zu_journal_recover(&j)) return 1;
        for (unsigned i = 0; i < 512; i++) if (d.data[20][i] != 0x11) return 1;
    }
    {
        struct test_disk d = {0}; memset(d.data[20], 0x11, 512);
        struct zu_journal j = {{&d, rd, wr, sync_disk, 32, 1}, 20, 25, 4, 0};
        if (!zu_journal_begin(&j) || !zu_journal_write(&j, 20, first)) return 1;
        memcpy(d.data[6], "ZUOL", 4); /* torn shadow of next, unpublished record */
        if (!zu_journal_recover(&j)) return 1;
        for (unsigned i = 0; i < 512; i++) if (d.data[20][i] != 0x11) return 1;
    }
    {
        static struct large_disk d;
        memset(d.data[2048], 0x11, 512);
        struct zu_journal j = {{&d, large_read, large_write, large_flush, 2200, 34},
                                2048, 2100, ZU_JOURNAL_MAX_SLOTS, 0};
        if (!zu_journal_begin(&j)) return 1;
        for (unsigned i = 0; i < 546; i++)
            if (!zu_journal_write(&j, 2048, i & 1 ? first : second)) return 1;
        if (!zu_journal_recover(&j)) return 1;
        for (unsigned i = 0; i < 512; i++) if (d.data[2048][i] != 0x11) return 1;
        puts("PASS 650-slot journal rolls back 546 metadata writes");
    }
    puts("PASS journal interruption, commit, torn headers and undo descriptors");
    return 0;
}
