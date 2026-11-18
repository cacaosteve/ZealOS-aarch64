#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include "../src/efi_fat32.h"
struct disk { FILE *file; unsigned writes; };
static int rd(void *c, uint64_t lba, uint8_t out[512]) {
    struct disk *d = c;
    return !fseeko(d->file, (off_t)(lba * 512), SEEK_SET) && fread(out, 1, 512, d->file) == 512;
}
static int wr(void *c, uint64_t lba, const uint8_t in[512]) {
    struct disk *d = c;
    d->writes++;
    return !fseeko(d->file, (off_t)(lba * 512), SEEK_SET) && fwrite(in, 1, 512, d->file) == 512;
}
static int sync_disk(void *c) {
    struct disk *d = c; return !fflush(d->file) && !fsync(fileno(d->file));
}
int main(int argc, char **argv) {
    static uint8_t bytes[16304626];
    static const uint8_t name[11] = {'Z','E','A','L','T','S','T',' ','B','I','N'};
    if (argc != 2) return 2;
    struct disk d = {fopen(argv[1], "r+b"), 0}; if (!d.file) return 1;
    for (unsigned i = 0; i < sizeof(bytes); i++) bytes[i] = (uint8_t)(i * 73u + i / 19u);
    if (efi_fat32_create_root(rd, wr, sync_disk, &d, 2048, 128863, name, bytes, UINT32_MAX) || d.writes)
        return 1;
    if (!efi_fat32_create_root(rd, wr, sync_disk, &d, 2048, 128863, name, bytes, sizeof(bytes))) {
        fputs("file creation failed\n", stderr); return 1;
    }
    unsigned writes = d.writes;
    if (efi_fat32_create_root(rd, wr, sync_disk, &d, 2048, 128863, name, bytes, sizeof(bytes)) || d.writes != writes) {
        fputs("duplicate accepted\n", stderr); return 1;
    }
    fclose(d.file);
    puts("PASS create and duplicate rejection");
    return 0;
}
