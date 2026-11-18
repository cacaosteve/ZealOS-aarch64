#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include "../src/efi_fat32_reclaim.h"
struct disk { FILE *f; uint64_t writes, stop; int tear; };
static int rd(void *ctx, uint64_t lba, uint8_t b[512]) {
    struct disk *d = ctx;
    return !fseeko(d->f, (off_t)(lba * 512), SEEK_SET) && fread(b, 1, 512, d->f) == 512;
}
static int wr(void *ctx, uint64_t lba, const uint8_t b[512]) {
    struct disk *d = ctx;
    if (fseeko(d->f, (off_t)(lba * 512), SEEK_SET)) return 0;
    if (++d->writes == d->stop) {
        if (d->tear) { (void)fwrite(b, 1, (size_t)d->tear, d->f); (void)fflush(d->f); }
        return 0;
    }
    return fwrite(b, 1, 512, d->f) == 512;
}
static int sync_disk(void *ctx) { struct disk *d = ctx; return !fflush(d->f) && !fsync(fileno(d->f)); }
int main(int argc, char **argv) {
    static struct efi_fat32_ownership owner;
    static struct efi_reclaim_batch batch;
    if (argc != 4) return 2;
    struct disk d = {fopen(argv[1], "r+b"), 0, strtoull(argv[3], NULL, 10), 7};
    if (!d.f || fseeko(d.f, 0, SEEK_END)) return 1;
    off_t size = ftello(d.f); if (size < 0 || size % 512) return 1;
    struct zu_journal j = {{&d, rd, wr, sync_disk, (uint64_t)size / 512, 34},
                           2048, 130911, ZU_JOURNAL_MAX_SLOTS, 0};
    int ok = 0, done = 0;
    if (!strcmp(argv[2], "start")) ok = efi_reclaim_start(&j, &owner);
    else if (!strncmp(argv[2], "root", 4) && argv[2][4] >= '0' && argv[2][4] <= '2' && !argv[2][5])
        ok = efi_reclaim_start_inactive(&j, &owner, (unsigned)(argv[2][4] - '0'));
    else if (!strcmp(argv[2], "recover")) ok = zu_journal_recover(&j);
    else if (!strcmp(argv[2], "step")) ok = efi_reclaim_step(&j, &owner, &batch, &done);
    else if (!strcmp(argv[2], "space")) {
        struct efi_fat32_space space;
        ok = efi_fat32_scan_space(rd, &d, 2048, 128863, &space);
        if (ok) printf("space=%u\n", space.free_clusters);
    }
    else if (!strcmp(argv[2], "status")) {
        struct efi_source_reader reader; struct efi_reclaim_cursor cursor;
        ok = efi_reclaim_reader(rd, &d, 2048, 128863, &reader) &&
             efi_reclaim_find_active(&reader, &cursor) == 1;
    }
    else return 2;
    if (d.stop && d.writes == d.stop && !ok) { puts("INTERRUPTED"); ok = 1; }
    else if (ok) printf("PASS %s done=%d writes=%llu\n", argv[2], done, (unsigned long long)d.writes);
    fclose(d.f); return ok ? 0 : 1;
}
