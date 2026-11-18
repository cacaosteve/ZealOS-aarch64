/* Initialize a stock RedSea volume and two empty source banks inside the
 * partition-relative SD backend. The boot sector is the final commit. */
#pragma once

#include "block_device.h"

#define PI4_INSTALL_ARCHIVE "SOURCES.TAR"
#define PI4_INSTALL_ARCHIVE_MAX (64u * 1024u * 1024u)

static void pi4_install_put_le(uint8_t *out, uint64_t value, unsigned bytes) {
    for (unsigned i = 0; i < bytes; i++) out[i] = (uint8_t)(value >> (8u * i));
}

static void pi4_install_dirent(uint8_t *out, const char *name, uint16_t attr,
                              uint64_t first, uint64_t size) {
    pi4_install_put_le(out, attr, 2);
    for (unsigned i = 0; name[i]; i++) out[2 + i] = (uint8_t)name[i];
    pi4_install_put_le(out + 40, first, 8);
    pi4_install_put_le(out + 48, size, 8);
}

static int pi4_install_write_verified(uint64_t lba, const uint8_t block[512]) {
    uint8_t check[512];
    if (!zbd_write_abs_many(block, lba, 1) ||
        !zbd_read_abs_many(check, lba, 1))
        return 0;
    for (unsigned i = 0; i < sizeof(check); i++)
        if (check[i] != block[i]) return 0;
    return 1;
}

static int pi4_sd_install_fits(uint64_t length) {
    uint64_t sectors = g_zbd.redsea_sectors;
    uint64_t bitmap = (sectors + 4095u) / 4096u;
    return zbd_ready() && g_zbd.lba_offset && !g_zbd.redsea_lba &&
           g_zbd.flush_supported && length && length <= PI4_INSTALL_ARCHIVE_MAX &&
           bitmap + 12u + (length + 511u) / 512u <= sectors;
}

static int pi4_sd_install(const uint8_t *archive, uint64_t length, uint64_t id) {
    uint8_t block[512];
    static const uint8_t zero[512];
    const uint16_t dir = 0x410u, file = 0x420u;
    uint64_t sectors = g_zbd.redsea_sectors;
    uint64_t bitmap = (sectors + 4095u) / 4096u;
    uint64_t root = bitmap + 1, home = root + 1, tmp = home + 8, first = tmp + 1;
    uint64_t end = first + (length + 511u) / 512u;

    if (!archive || !pi4_sd_install_fits(length) ||
        !pi4_install_write_verified(0, zero) || !zbd_flush())
        return 0;

    for (uint64_t lba = 1; lba <= bitmap; lba++) {
        uint64_t base_bit = (lba - 1) * 4096u;
        uint64_t used = end - bitmap;
        for (unsigned i = 0; i < sizeof(block); i++) {
            uint64_t bit = base_bit + i * 8u;
            block[i] = bit >= used ? 0 :
                       used - bit >= 8 ? 0xff : (uint8_t)((1u << (used - bit)) - 1u);
        }
        if (!pi4_install_write_verified(lba, block)) return 0;
    }

    for (unsigned i = 0; i < sizeof(block); i++) block[i] = 0;
    pi4_install_dirent(block, ".", dir, root, 512);
    pi4_install_dirent(block + 64, "..", dir, root, 0);
    pi4_install_dirent(block + 128, "Home", dir, home, 8u * 512u);
    pi4_install_dirent(block + 192, "Tmp", dir, tmp, 512);
    pi4_install_dirent(block + 256, PI4_INSTALL_ARCHIVE, file, first, length);
    if (!pi4_install_write_verified(root, block)) return 0;

    for (unsigned i = 0; i < sizeof(block); i++) block[i] = 0;
    pi4_install_dirent(block, ".", dir, home, 8u * 512u);
    pi4_install_dirent(block + 64, "..", dir, root, 0);
    if (!pi4_install_write_verified(home, block)) return 0;
    for (uint64_t lba = home + 1; lba < tmp; lba++)
        if (!pi4_install_write_verified(lba, zero)) return 0;
    for (unsigned i = 0; i < sizeof(block); i++) block[i] = 0;
    pi4_install_dirent(block, ".", dir, tmp, 512);
    pi4_install_dirent(block + 64, "..", dir, root, 0);
    if (!pi4_install_write_verified(tmp, block)) return 0;

    for (uint64_t off = 0; off < length; off += 512u) {
        for (unsigned i = 0; i < sizeof(block); i++)
            block[i] = off + i < length ? archive[off + i] : 0;
        if (!pi4_install_write_verified(first + off / 512u, block)) return 0;
    }

    /* The first empty record terminates each bank, including on reinstall. */
    for (unsigned i = 0; i < sizeof(block); i++) block[i] = 0;
    for (unsigned i = 0; i < 8; i++) block[i] = (uint8_t)"ZCSRC001"[i];
    pi4_install_put_le(block + 8, 1, 4);
    pi4_install_put_le(block + 12, g_zbd.source_a_sectors, 4);
    if (!pi4_install_write_verified(g_zbd.source_a_lba + 1, zero) ||
        !pi4_install_write_verified(g_zbd.source_a_lba, block)) return 0;
    pi4_install_put_le(block + 12, g_zbd.source_b_sectors, 4);
    if (!pi4_install_write_verified(g_zbd.source_b_lba + 1, zero) ||
        !pi4_install_write_verified(g_zbd.source_b_lba, block) || !zbd_flush())
        return 0;

    for (unsigned i = 0; i < sizeof(block); i++) block[i] = 0;
    block[3] = 0x88;
    pi4_install_put_le(block + 16, sectors, 8);
    pi4_install_put_le(block + 24, root, 8);
    pi4_install_put_le(block + 32, bitmap, 8);
    pi4_install_put_le(block + 40, id ? id : 1, 8);
    block[510] = 0x55;
    block[511] = 0xaa;
    return pi4_install_write_verified(0, block) && zbd_flush();
}
