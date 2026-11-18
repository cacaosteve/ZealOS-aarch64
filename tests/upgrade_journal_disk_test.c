#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include "../src/upgrade_journal.h"
struct file_disk { FILE *f; };
static int rd(void *p, uint64_t lba, uint8_t b[512]) {
    struct file_disk *d = p;
    return !fseeko(d->f, (off_t)(lba * 512), SEEK_SET) && fread(b, 1, 512, d->f) == 512;
}
static int wr(void *p, uint64_t lba, const uint8_t b[512]) {
    struct file_disk *d = p;
    return !fseeko(d->f, (off_t)(lba * 512), SEEK_SET) && fwrite(b, 1, 512, d->f) == 512;
}
static int flush(void *p) {
    struct file_disk *d = p; return !fflush(d->f) && !fsync(fileno(d->f));
}
int main(int argc, char **argv) {
    if (argc != 3) return 2;
    struct file_disk d = {fopen(argv[1], "r+b")};
    if (!d.f || fseeko(d.f, 0, SEEK_END)) return 1;
    off_t bytes = ftello(d.f);
    if (bytes <= 0 || bytes % 512) return 1;
    struct zu_journal j = {{&d, rd, wr, flush, (uint64_t)bytes / 512, 34},
                            2048, 130911, 4, 0};
    int ok = 0;
    if (!strcmp(argv[2], "interrupt")) {
        uint8_t sector[512];
        ok = zu_journal_begin(&j);
        const uint64_t targets[] = {2048, 2080, 3072};
        for (unsigned i = 0; ok && i < 3; i++) {
            ok = rd(&d, targets[i], sector);
            sector[100] ^= (uint8_t)(0x51 + i);
            if (ok) ok = zu_journal_write(&j, targets[i], sector);
        }
    } else if (!strcmp(argv[2], "recover")) ok = zu_journal_recover(&j);
    fclose(d.f);
    return ok ? 0 : 1;
}
