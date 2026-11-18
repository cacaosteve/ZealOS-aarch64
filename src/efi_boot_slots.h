/* Exact, bounded recognition of the three supported EFI boot configurations. */
#pragma once
#include "efi_fat32_read.h"
enum efi_boot_slot { EFI_BOOT_UNKNOWN = 0, EFI_BOOT_LEGACY = 1, EFI_BOOT_U = 2, EFI_BOOT_V = 3 };
static const uint8_t efi_config_legacy[] =
    "timeout: 0\nserial: yes\ngraphics: yes\ndefault_entry: 1\nquiet: no\n\n"
    "/ZealOS aarch64 zealbooter\n    protocol: limine\n"
    "    path: boot():/boot/zealbooter.elf\n    module_path: boot():/boot/kernel.elf\n"
    "    module_path: boot():/boot/demo.hcbc\n    module_path: boot():/boot/sources.tar\n"
    "    kaslr: no\n    resolution: 800x600x32\n";
static const uint8_t efi_config_u[] =
    "timeout: 0\nserial: yes\ngraphics: yes\ndefault_entry: 1\nquiet: no\n\n"
    "/ZealOS aarch64 zealbooter\n    protocol: limine\n"
    "    path: boot():/ZUBOOTER.ELF\n    module_path: boot():/ZUKERNEL.ELF\n"
    "    module_path: boot():/boot/demo.hcbc\n    module_path: boot():/ZUSOURCE.LZ4\n"
    "    kaslr: no\n    resolution: 800x600x32\n";
static const uint8_t efi_config_v[] =
    "timeout: 0\nserial: yes\ngraphics: yes\ndefault_entry: 1\nquiet: no\n\n"
    "/ZealOS aarch64 zealbooter\n    protocol: limine\n"
    "    path: boot():/ZVBOOTER.ELF\n    module_path: boot():/ZVKERNEL.ELF\n"
    "    module_path: boot():/boot/demo.hcbc\n    module_path: boot():/ZVSOURCE.LZ4\n"
    "    kaslr: no\n    resolution: 800x600x32\n";
static int efi_config_matches(const uint8_t b[512], const uint8_t *expected, uint32_t n) {
    uint32_t size = sizeof(efi_config_legacy) - 1;
    if (n > size) return 0;
    for (uint32_t i = 0; i < size; i++) if (b[i] != (i < n ? expected[i] : '\n')) return 0;
    return 1;
}
static enum efi_boot_slot efi_boot_slot_read(zeal_partition_read_fn read, void *ctx,
                                              uint64_t first, uint64_t sectors) {
    static const uint8_t name[11] = {'L','I','M','I','N','E','~','1','C','O','N'};
    struct efi_source_reader r; uint8_t b[512], boot[512]; uint32_t cluster, bytes, next;
    if (!read || !efi_fat32_scan_space(read, ctx, first, sectors, &r.space) || !read(ctx, first, boot)) return EFI_BOOT_UNKNOWN;
    r.read = read; r.context = ctx; r.root = zeal_partition_u32(boot + 44); r.cached_fat_sector = UINT32_MAX;
    if (!efi_source_find(&r, r.root, name, 0, &cluster, &bytes) || bytes != sizeof(efi_config_legacy) - 1 ||
        !efi_source_next_cluster(&r, cluster, &next) || next < 0x0ffffff8u ||
        !read(ctx, r.space.data_first + (uint64_t)(cluster - 2) * r.space.sectors_per_cluster, b)) return EFI_BOOT_UNKNOWN;
    if (efi_config_matches(b, efi_config_legacy, sizeof(efi_config_legacy) - 1)) return EFI_BOOT_LEGACY;
    if (efi_config_matches(b, efi_config_u, sizeof(efi_config_u) - 1)) return EFI_BOOT_U;
    if (efi_config_matches(b, efi_config_v, sizeof(efi_config_v) - 1)) return EFI_BOOT_V;
    return EFI_BOOT_UNKNOWN;
}
