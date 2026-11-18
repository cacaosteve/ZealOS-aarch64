#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include "../src/upgrade_preflight.h"
#include "../src/source_upgrade_stage.h"
#include "../src/source_upgrade_pending.h"
#include <string.h>
struct disk { FILE *file; uint32_t writes, stop; int tear; };
static int rd(void *ctx, uint64_t lba, uint8_t b[512]) {
    struct disk *d = ctx;
    return !fseeko(d->file, (off_t)(lba * 512), SEEK_SET) && fread(b, 1, 512, d->file) == 512;
}
static int wr(void *ctx, uint64_t lba, const uint8_t b[512]) {
    struct disk *d = ctx;
    if (++d->writes == d->stop) {
        if (d->tear && !fseeko(d->file, (off_t)(lba * 512), SEEK_SET)) {
            (void)fwrite(b, 1, (size_t)d->tear, d->file); (void)fflush(d->file);
        }
        return 0;
    }
    return !fseeko(d->file, (off_t)(lba * 512), SEEK_SET) && fwrite(b, 1, 512, d->file) == 512;
}
static int sync_disk(void *ctx) { struct disk *d = ctx; return !fflush(d->file) && !fsync(fileno(d->file)); }
int main(int argc, char **argv) {
    static struct zu_source_scan scan, after;
    static struct zu_source_stage stage;
    struct zeal_upgrade_target target;
    struct efi_source_reader reader;
    struct zu_source_stats stats;
    if (argc != 3 && argc != 4) return 2;
    int publish = argc == 4 && !strcmp(argv[3], "publish");
    struct disk d = {fopen(argv[1], "r+b"), 0,
                     argc == 4 && !publish ? (uint32_t)strtoul(argv[3], NULL, 10) : 0, 0};
    FILE *f = fopen(argv[2], "rb");
    if (!d.file || !f || fseeko(d.file, 0, SEEK_END) || fseeko(f, 0, SEEK_END)) return 1;
    off_t disk_bytes = ftello(d.file), tar_bytes = ftello(f);
    if (disk_bytes <= 0 || disk_bytes % 512 || tar_bytes <= 0 || fseeko(f, 0, SEEK_SET)) return 1;
    uint8_t *tar_data = malloc((size_t)tar_bytes);
    if (!tar_data || fread(tar_data, 1, (size_t)tar_bytes, f) != (size_t)tar_bytes) return 1;
    if (!zeal_upgrade_preflight(rd, &d, (uint64_t)disk_bytes / 512, &target) ||
        !zu_source_catalog(rd, &d, target.bank_a, target.bank_b, &scan) ||
        !efi_source_open(rd, &d, target.esp_first, target.esp_sectors, &reader)) return 1;
    struct efi_source_tar baseline = {.reader = &reader};
    if (!zu_source_baseline_compare(&scan, &baseline, &stats) ||
        !zu_source_incoming_index(&stage, tar_data, (size_t)tar_bytes) ||
        !zu_source_make_plan(&scan, &stage)) return 1;
    int staged = zu_source_stage_inactive(&scan, &stage, target.bank_a, target.bank_b,
                                           (uint64_t)disk_bytes / 512, wr, sync_disk);
    if ((d.stop ? staged || d.writes != d.stop : !staged) ||
        !zu_source_catalog(rd, &d, target.bank_a, target.bank_b, &after) ||
        after.bank != scan.bank || after.generation != scan.generation) return 1;
    if (publish) {
        struct zu_source_pending pending = {scan.bank, stage.target, (uint64_t)disk_bytes / 512,
                                             scan.generation, stage.target_generation, stage.needed, 0, 0};
        if (!zu_pending_range_crc(rd, &d, scan.bank, 65536, &pending.active_crc) ||
            !zu_pending_range_crc(rd, &d, stage.target + 1,
                                  stage.needed < 65536 ? stage.needed : stage.needed - 1,
                                  &pending.candidate_crc) ||
            !zu_pending_prepare(rd, wr, sync_disk, &d, &pending)) return 1;
        struct zu_source_pending recovered;
        if (zu_pending_read(rd, &d, (uint64_t)disk_bytes / 512,
                            target.bank_a, target.bank_b, &recovered) != 1) return 1;
        /* Cleanup can be restarted if clearing retired journal records tears. */
        struct zu_journal j = {{&d, rd, wr, sync_disk, (uint64_t)disk_bytes / 512, 34},
                               2048, 130911, ZU_JOURNAL_MAX_SLOTS, 0};
        uint8_t saved_active[512], changed_active[512];
        if (!rd(&d, scan.bank + 1, saved_active)) return 1;
        memcpy(changed_active, saved_active, 512); changed_active[511] ^= 1;
        if (!wr(&d, scan.bank + 1, changed_active) || !sync_disk(&d) ||
            zu_pending_cancel(&j, &recovered) || !wr(&d, scan.bank + 1, saved_active) || !sync_disk(&d)) return 1;
        d.stop = d.writes + 25; d.tear = 1;
        if (zu_pending_cancel(&j, &recovered)) return 1;
        d.stop = 0; d.tear = 0;
        if (zu_pending_read(rd, &d, (uint64_t)disk_bytes / 512,
                            target.bank_a, target.bank_b, &recovered) != 1 ||
            !zu_pending_cancel(&j, &recovered) ||
            zu_pending_read(rd, &d, (uint64_t)disk_bytes / 512,
                            target.bank_a, target.bank_b, &recovered) != 2 ||
            !zu_pending_prepare(rd, wr, sync_disk, &d, &pending)) return 1;
        /* Interrupt both marker writes at several positions. */
        const int tear_sizes[] = {1, 7, 16, 31, 52, 511};
        for (unsigned copy = 0; copy < 2; copy++)
        for (unsigned k = 0; k < sizeof(tear_sizes) / sizeof(tear_sizes[0]); k++) {
            d.stop = d.writes + 1952 + copy; d.tear = tear_sizes[k];
            if (zu_pending_cancel(&j, &recovered)) return 1;
            d.stop = 0; d.tear = 0;
            int state = zu_pending_read(rd, &d, (uint64_t)disk_bytes / 512,
                                        target.bank_a, target.bank_b, &recovered);
            if (state == 1 && !zu_pending_cancel(&j, &recovered)) return 1;
            if (state != 1 && state != 2) return 1;
            if (!zu_pending_prepare(rd, wr, sync_disk, &d, &pending)) return 1;
        }
        uint8_t saved[512], changed[512], current_header[512];
        if (!rd(&d, scan.bank + 1, saved)) return 1;
        for (unsigned i = 0; i < 512; i++) changed[i] = saved[i];
        changed[511] ^= 0x40;
        if (!wr(&d, scan.bank + 1, changed) || !sync_disk(&d) ||
            zu_pending_publish(rd, wr, sync_disk, &d, &recovered) ||
            !rd(&d, stage.target, current_header) || !zu_empty(current_header) ||
            !wr(&d, scan.bank + 1, saved) || !sync_disk(&d)) return 1;
        if (!rd(&d, stage.target + 1, saved)) return 1;
        for (unsigned i = 0; i < 512; i++) changed[i] = saved[i];
        changed[511] ^= 0x40;
        if (!wr(&d, stage.target + 1, changed) || !sync_disk(&d) ||
            zu_pending_publish(rd, wr, sync_disk, &d, &recovered) ||
            !rd(&d, stage.target, current_header) || !zu_empty(current_header) ||
            !wr(&d, stage.target + 1, saved) || !sync_disk(&d)) return 1;
        /* A torn candidate-superblock write must be retryable from the marker. */
        d.stop = d.writes + 1; d.tear = 1;
        if (zu_pending_publish(rd, wr, sync_disk, &d, &recovered)) return 1;
        d.stop = 0; d.tear = 0;
        if (zu_pending_read(rd, &d, (uint64_t)disk_bytes / 512,
                            target.bank_a, target.bank_b, &recovered) != 1 ||
            !zu_pending_publish(rd, wr, sync_disk, &d, &recovered) ||
            !zu_source_catalog(rd, &d, target.bank_a, target.bank_b, &after) ||
            after.bank != stage.target || after.generation != stage.target_generation) return 1;
    }
    printf("PASS %s unpublished bank %llu generation %u: refreshed %u preserved %u added %u sectors %u\n",
           publish ? "published after retry" : d.stop ? "interrupted" : "staged",
           (unsigned long long)stage.target, stage.target_generation, stage.refreshed,
           stage.preserved, stage.added, stage.needed);
    free(tar_data); fclose(f); fclose(d.file);
    return 0;
}
