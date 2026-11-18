#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include "../src/efi_fat32.h"
#include "../src/upgrade_journal.h"
struct disk { FILE *file; struct zu_journal journal; unsigned meta, stop; };
static int rd(void *p, uint64_t lba, uint8_t out[512]) {
    struct disk *d = p;
    return !fseeko(d->file, (off_t)(lba * 512), SEEK_SET) && fread(out, 1, 512, d->file) == 512;
}
static int wr(void *p, uint64_t lba, const uint8_t in[512]) {
    struct disk *d = p;
    return !fseeko(d->file, (off_t)(lba * 512), SEEK_SET) && fwrite(in, 1, 512, d->file) == 512;
}
static int sync_disk(void *p) {
    struct disk *d = p; return !fflush(d->file) && !fsync(fileno(d->file));
}
static int metadata(void *p, uint64_t lba, const uint8_t in[512]) {
    struct disk *d = p;
    if (!zu_journal_write(&d->journal, lba, in)) return 0;
    return ++d->meta != d->stop;
}
int main(int argc, char **argv) {
    static uint8_t data[100003];
    static const uint8_t name[11] = {'Z','E','A','L','T','S','T',' ','B','I','N'};
    if (argc != 4 && argc != 5) return 2;
    struct disk d = {0}; d.file = fopen(argv[1], "r+b");
    if (!d.file || fseeko(d.file, 0, SEEK_END)) return 1;
    off_t size = ftello(d.file);
    if (size <= 0 || size % 512) return 1;
    unsigned slots = argc == 5 ? (unsigned)strtoul(argv[4], 0, 10) : 512;
    d.journal = (struct zu_journal){{&d, rd, wr, sync_disk, (uint64_t)size / 512, 34},
                                    2048, 130911, slots, 0};
    d.stop = (unsigned)strtoul(argv[3], 0, 10);
    int ok = 0;
    if (!strcmp(argv[2], "recover")) ok = zu_journal_recover(&d.journal);
    else if (!strcmp(argv[2], "stage")) {
        for (unsigned i = 0; i < sizeof(data); i++) data[i] = (uint8_t)(i * 73u + i / 19u);
        if (!zu_journal_begin(&d.journal)) return 1;
        int created = efi_fat32_create_root_ex(rd, wr, metadata, sync_disk, &d,
                                                2048, 128863, name, data, sizeof(data));
        ok = d.stop ? (!created && d.meta == d.stop) : (created && zu_journal_commit(&d.journal));
    }
    fclose(d.file);
    return ok ? 0 : 1;
}
