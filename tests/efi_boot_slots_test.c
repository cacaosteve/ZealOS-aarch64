#include <stdio.h>
#include <stdint.h>
#include <sys/types.h>
#include "../src/efi_boot_slots.h"
static int rd(void *ctx, uint64_t lba, uint8_t out[512]) {
    FILE *f = ctx;
    return !fseeko(f, (off_t)(lba * 512), SEEK_SET) && fread(out, 1, 512, f) == 512;
}
int main(int argc, char **argv) {
    if (argc != 3) return 2;
    FILE *f = fopen(argv[1], "rb"); if (!f) return 1;
    int slot = efi_boot_slot_read(rd, f, 2048, 128863);
    fclose(f);
    if (slot != argv[2][0] - '0') return 1;
    printf("PASS boot slot %d\n", slot); return 0;
}
