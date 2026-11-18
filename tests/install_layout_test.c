#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/install_layout.h"
static uint8_t *image;
static int read_template(void *ctx, uint64_t sector, uint8_t *out) {
    (void)ctx;
    if (sector >= ZEAL_DISK_SECTS) return 0;
    memcpy(out, image + sector * 512, 512); return 1;
}
static int read_layout(void *ctx, uint64_t sector, uint8_t *out) {
    return zi_layout_sector(ctx, sector, out);
}
int main(int argc, char **argv) {
    assert(argc == 2);
    FILE *f = fopen(argv[1], "rb"); assert(f);
    image = malloc(ZEAL_DISK_SECTS * 512); assert(image);
    assert(fread(image, 512, ZEAL_DISK_SECTS, f) == ZEAL_DISK_SECTS); fclose(f);
    static struct zi_layout l;
    uint8_t block[512], original[512];
    assert(!zi_layout_init(&l, ZEAL_DISK_SECTS - 1, read_template, 0));
    assert(!zi_layout_init(&l, UINT64_MAX, read_template, 0));
    assert(zi_layout_init(&l, ZEAL_DISK_SECTS, read_template, 0));
    for (uint64_t i = 0; i < ZEAL_DISK_SECTS; i++) {
        assert(zi_layout_sector(&l, i, block));
        assert(!memcmp(block, image + i * 512, 512));
    }
    uint64_t sizes[] = {ZEAL_DISK_SECTS + 1, 256 * 2048, 1024 * 2048,
                        8ull * 1024 * 2048, 5ull * 1024 * 1024 * 2048};
    for (unsigned i = 0; i < sizeof(sizes)/sizeof(*sizes); i++) {
        assert(zi_layout_init(&l, sizes[i], read_template, 0));
        struct zeal_partition parts[16], data; size_t count;
        assert(zeal_partition_scan(read_layout, &l, sizes[i], parts, 16, &count) == 1);
        assert(count == 2 && zeal_partition_find_data(parts, count, &data) == 1);
        assert(data.first_lba == ZEAL_RS_LBA_BASE);
        assert(data.sector_count == sizes[i] - 33 - ZEAL_RS_LBA_BASE);
        assert(l.redsea > ZEAL_RS_SECTS && l.bank_b + ZEAL_SRC_SHADOW_SECTS == sizes[i]-33);
        for (uint64_t n = 2048; n < ZEAL_RS_LBA_BASE; n++) {
            assert(zi_layout_sector(&l, n, block));
            assert(!memcmp(block, image + n * 512, 512));
        }
        for (uint64_t n = 0; n < ZEAL_SRC_SECTS + ZEAL_SRC_SHADOW_SECTS; n++) {
            assert(zi_layout_sector(&l, l.bank_a+n, block));
            assert(read_template(0, ZEAL_SRC_LBA_BASE+n, original));
            assert(!memcmp(block, original, 512));
        }
        assert(zi_layout_sector(&l, sizes[i]-1, block));
        uint32_t crc = zeal_partition_u32(block+16); zi_put(block+16, 0, 4);
        assert(zeal_partition_crc(block, 92) == crc);
        assert(zeal_partition_u64(block+24) == sizes[i]-1);
        assert(zeal_partition_u64(block+32) == 1);
        assert(zi_layout_sector(&l, ZEAL_RS_LBA_BASE, block));
        assert(block[3] == 0x88 && zeal_partition_u64(block+16) == l.redsea);
        assert(zi_layout_sector(&l, ZEAL_RS_LBA_BASE+1, block));
        assert(block[0] == 0xff && block[1] == 7);
        assert(!zi_layout_sector(&l, sizes[i], block));
    }
    free(image);
    puts("PASS legacy byte identity, expanded GPT/RedSea bounds, EFI and seeded banks (128 MiB+512 through 5 TiB)");
}
