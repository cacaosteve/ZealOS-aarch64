/* Experimental reclaim cursor. Caller must independently prove the old
 * archive is no longer referenced by the active boot configuration and must
 * hold exclusive access. All metadata updates go through the supplied undo
 * journal. This is not connected to the installer. */
#pragma once
#include "efi_fat32_reclaim_plan.h"
#include "efi_source_tar.h"
#include "efi_boot_slots.h"
#include "upgrade_journal.h"
#define EFI_RECLAIM_BATCH_FATS 200u
struct efi_reclaim_cursor { uint64_t lba; unsigned offset; uint32_t next, remaining; };
struct efi_reclaim_batch {
    uint32_t indexes[EFI_RECLAIM_BATCH_FATS];
    uint8_t sectors[EFI_RECLAIM_BATCH_FATS][512];
    uint32_t count;
};
static void efi_reclaim_encode(uint8_t *entry, uint32_t next, uint32_t remaining) {
    for (unsigned i = 0; i < 32; i++) entry[i] = 0;
    entry[0] = 0xe5;
    const char *magic = "ZURECL01";
    for (unsigned i = 0; i < 8; i++) entry[i + 1] = (uint8_t)magic[i];
    zu_put32(entry + 9, next); zu_put32(entry + 13, remaining);
    zu_put32(entry + 17, zeal_partition_crc(entry, 17));
}
static int efi_reclaim_decode(const uint8_t *e, uint32_t *next, uint32_t *remaining) {
    const char *magic = "ZURECL01";
    if (e[0] != 0xe5) return 0;
    for (unsigned i = 0; i < 8; i++) if (e[i + 1] != (uint8_t)magic[i]) return 0;
    if (zeal_partition_u32(e + 17) != zeal_partition_crc(e, 17)) return 0;
    *next = zeal_partition_u32(e + 9); *remaining = zeal_partition_u32(e + 13);
    return (!!*next == !!*remaining);
}
/* Find either the unique original entry or the unique cursor in /boot. */
static int efi_reclaim_find_dir(struct efi_source_reader *r, uint32_t dir, int marker,
                                const uint8_t name[11], struct efi_reclaim_cursor *out) {
    uint32_t cluster, found = 0; uint8_t b[512]; int ended = 0;
    cluster = dir;
    for (uint32_t visited = 0; !ended && visited < r->space.clusters; visited++) {
        if (cluster < 2 || cluster > r->space.clusters + 1) return -1;
        uint64_t base = r->space.data_first + (uint64_t)(cluster - 2) * r->space.sectors_per_cluster;
        for (uint32_t n = 0; n < r->space.sectors_per_cluster && !ended; n++) {
            if (!r->read(r->context, base + n, b)) return -1;
            for (unsigned i = 0; i < 512; i += 32) {
                uint8_t *e = b + i; uint32_t next = 0, remaining = 0;
                if (!e[0]) { ended = 1; break; }
                int match = marker ? efi_reclaim_decode(e, &next, &remaining) : 0;
                if (!marker && e[0] != 0xe5 && e[11] != 0x0f && !(e[11] & 0x18)) {
                    unsigned k = 0; while (k < 11 && e[k] == name[k]) k++;
                    match = k == 11;
                    if (match) {
                        next = ((uint32_t)efi_fat32_u16(e + 20) << 16) | efi_fat32_u16(e + 26);
                        uint32_t bytes = zeal_partition_u32(e + 28);
                        uint64_t cb = (uint64_t)r->space.sectors_per_cluster * 512;
                        remaining = (uint32_t)(((uint64_t)bytes + cb - 1) / cb);
                        if (!remaining || next < 2) return -1;
                    }
                }
                if (match) { found++; out->lba = base + n; out->offset = i; out->next = next; out->remaining = remaining; }
            }
        }
        if (!ended) {
            uint32_t next;
            if (!efi_source_next_cluster(r, cluster, &next)) return -1;
            if (next >= 0x0ffffff8u) ended = 1;
            else cluster = next;
        }
    }
    return !ended || found > 1 ? -1 : (int)found;
}
static int efi_reclaim_find(struct efi_source_reader *r, int marker, struct efi_reclaim_cursor *out) {
    static const uint8_t boot_name[11] = {'B','O','O','T',' ',' ',' ',' ',' ',' ',' '};
    static const uint8_t name[11] = {'S','O','U','R','C','E','S',' ','T','A','R'};
    uint32_t dir, ignored;
    if (!efi_source_find(r, r->root, boot_name, 1, &dir, &ignored)) return -1;
    return efi_reclaim_find_dir(r, dir, marker, name, out);
}
static int efi_reclaim_find_active(struct efi_source_reader *r, struct efi_reclaim_cursor *out) {
    struct efi_reclaim_cursor boot, root;
    int have_boot = efi_reclaim_find(r, 1, &boot);
    int have_root = efi_reclaim_find_dir(r, r->root, 1, NULL, &root);
    if (have_boot < 0 || have_root < 0 || (have_boot && have_root)) return -1;
    if (have_boot == 1) { *out = boot; return 1; }
    if (have_root == 1) { *out = root; return 1; }
    return 0;
}
static int efi_reclaim_reader(zeal_partition_read_fn read, void *ctx, uint64_t first, uint64_t sectors,
                              struct efi_source_reader *r) {
    uint8_t boot[512];
    if (!efi_fat32_scan_space(read, ctx, first, sectors, &r->space) || !read(ctx, first, boot)) return 0;
    r->read = read; r->context = ctx; r->root = zeal_partition_u32(boot + 44);
    r->cached_fat_sector = UINT32_MAX; return 1;
}
/* Reclaim is allowed only when the exact installed configuration points to
 * the new EFI files and the compressed archive is completely verified. */
static int efi_reclaim_boot_safe(zeal_partition_read_fn read, void *ctx) {
    static struct efi_compressed_source source;
    static const uint8_t kernel_names[2][11] = {{'Z','U','K','E','R','N','E','L','E','L','F'},
                                               {'Z','V','K','E','R','N','E','L','E','L','F'}};
    static const uint8_t booter_names[2][11] = {{'Z','U','B','O','O','T','E','R','E','L','F'},
                                               {'Z','V','B','O','O','T','E','R','E','L','F'}};
    struct efi_source_reader r; struct efi_source_tar tar;
    uint8_t b[512]; uint32_t cluster, bytes;
    enum efi_boot_slot slot = efi_boot_slot_read(read, ctx, 2048, 128863);
    if ((slot != EFI_BOOT_U && slot != EFI_BOOT_V) ||
        !efi_reclaim_reader(read, ctx, 2048, 128863, &r)) return 0;
    unsigned selected = slot == EFI_BOOT_V;
    const uint8_t *names[2] = {kernel_names[selected], booter_names[selected]};
    for (unsigned k = 0; k < 2; k++) {
        if (!efi_source_find(&r, r.root, names[k], 0, &cluster, &bytes) || bytes < 64 ||
            !read(ctx, r.space.data_first + (uint64_t)(cluster - 2) * r.space.sectors_per_cluster, b) ||
            b[0] != 0x7f || b[1] != 'E' || b[2] != 'L' || b[3] != 'F') return 0;
    }
    if (!efi_compressed_source_open_slot(read, ctx, 2048, 128863, &source, &tar, selected)) return 0;
    int status; struct efi_source_tar_entry entry;
    while ((status = efi_source_tar_next(&tar, &entry)) == 1) {
        while (tar.remaining) {
            uint32_t used;
            if (!efi_source_tar_data(&tar, b, &used)) return 0;
        }
    }
    return status == 0 && !source.left;
}
static int efi_reclaim_start(struct zu_journal *j, struct efi_fat32_ownership *owner) {
    struct efi_source_reader r; struct efi_reclaim_cursor c, existing; uint8_t b[512];
    if (!j || !efi_reclaim_boot_safe(j->io.read, j->io.context) ||
        !efi_fat32_ownership_check(j->io.read, j->io.context, 2048, 128863, owner) ||
        !efi_source_open(j->io.read, j->io.context, 2048, 128863, &r) ||
        efi_reclaim_find_active(&r, &existing) != 0 || efi_reclaim_find(&r, 0, &c) != 1 ||
        !j->io.read(j->io.context, c.lba, b) || !zu_journal_begin(j)) return 0;
    efi_reclaim_encode(b + c.offset, c.next, c.remaining);
    return zu_journal_write(j, c.lba, b) && zu_journal_commit(j);
}
/* Reclaim only one of the three known files in the inactive root slot. */
static int efi_reclaim_start_inactive(struct zu_journal *j, struct efi_fat32_ownership *owner,
                                      unsigned which) {
    static const uint8_t names[2][3][11] = {
        {{'Z','U','S','O','U','R','C','E','L','Z','4'}, {'Z','U','K','E','R','N','E','L','E','L','F'},
         {'Z','U','B','O','O','T','E','R','E','L','F'}},
        {{'Z','V','S','O','U','R','C','E','L','Z','4'}, {'Z','V','K','E','R','N','E','L','E','L','F'},
         {'Z','V','B','O','O','T','E','R','E','L','F'}}};
    struct efi_source_reader r; struct efi_reclaim_cursor c, existing; uint8_t b[512];
    if (!j || which >= 3 || !efi_reclaim_boot_safe(j->io.read, j->io.context)) return 0;
    enum efi_boot_slot slot = efi_boot_slot_read(j->io.read, j->io.context, 2048, 128863);
    if ((slot != EFI_BOOT_U && slot != EFI_BOOT_V) ||
        !efi_fat32_ownership_check(j->io.read, j->io.context, 2048, 128863, owner) ||
        !efi_reclaim_reader(j->io.read, j->io.context, 2048, 128863, &r) ||
        efi_reclaim_find_active(&r, &existing) != 0 ||
        efi_reclaim_find_dir(&r, r.root, 0, names[slot == EFI_BOOT_U][which], &c) != 1 ||
        !j->io.read(j->io.context, c.lba, b) || !zu_journal_begin(j)) return 0;
    efi_reclaim_encode(b + c.offset, c.next, c.remaining);
    return zu_journal_write(j, c.lba, b) && zu_journal_commit(j);
}
/* Revalidate the remaining orphan chain against every live file, then free at
 * most 200 FAT sectors and advance its cursor in the same transaction. */
static int efi_reclaim_step(struct zu_journal *j, struct efi_fat32_ownership *owner,
                            struct efi_reclaim_batch *batch, int *done) {
    struct efi_source_reader r; struct efi_reclaim_cursor c; uint8_t b[512], mirror[512];
    if (!j || !owner || !batch || !done ||
        !efi_fat32_ownership_check(j->io.read, j->io.context, 2048, 128863, owner) ||
        !efi_reclaim_reader(j->io.read, j->io.context, 2048, 128863, &r) ||
        efi_reclaim_find_active(&r, &c) != 1) return 0;
    if (!c.remaining) {
        if (!j->io.read(j->io.context, c.lba, b) || !zu_journal_begin(j)) return 0;
        for (unsigned i = 1; i < 32; i++) b[c.offset + i] = 0;
        b[c.offset] = 0xe5;
        if (!zu_journal_write(j, c.lba, b) || !zu_journal_commit(j)) return 0;
        *done = 1; return 1;
    }
    uint32_t cluster = c.next, next;
    for (uint32_t i = 0; i < c.remaining; i++) {
        if (!efi_owner_mark(owner, cluster) || !efi_source_next_cluster(&r, cluster, &next)) return 0;
        if (i + 1 == c.remaining) { if (next < 0x0ffffff8u) return 0; }
        else { if (next < 2 || next > r.space.clusters + 1) return 0; cluster = next; }
    }
    batch->count = 0; cluster = c.next;
    while (c.remaining) {
        uint32_t index = cluster / 128, pos = 0;
        while (pos < batch->count && batch->indexes[pos] != index) pos++;
        if (pos == batch->count) {
            if (batch->count == EFI_RECLAIM_BATCH_FATS) break;
            if (!r.read(r.context, r.space.fat_first + index, batch->sectors[pos]) ||
                !r.read(r.context, r.space.fat_first + r.space.fat_sectors + index, mirror)) return 0;
            for (unsigned k = 0; k < 512; k++) if (mirror[k] != batch->sectors[pos][k]) return 0;
            batch->indexes[pos] = index; batch->count++;
        }
        if (!efi_source_next_cluster(&r, cluster, &next)) return 0;
        zu_put32(batch->sectors[pos] + (cluster % 128) * 4, 0);
        c.remaining--; cluster = c.remaining ? next : 0;
    }
    if (!batch->count || !j->io.read(j->io.context, c.lba, b) || !zu_journal_begin(j)) return 0;
    for (uint32_t i = 0; i < batch->count; i++) {
        uint64_t lba = r.space.fat_first + batch->indexes[i];
        if (!zu_journal_write(j, lba, batch->sectors[i]) ||
            !zu_journal_write(j, lba + r.space.fat_sectors, batch->sectors[i])) return 0;
    }
    uint8_t boot[512], fsinfo[512];
    if (!r.read(r.context, 2048, boot)) return 0;
    uint32_t fs = efi_fat32_u16(boot + 48), backup = efi_fat32_u16(boot + 50);
    uint32_t reserved = efi_fat32_u16(boot + 14);
    if (!fs || fs >= reserved || !backup || backup + fs >= reserved) return 0;
    for (unsigned copy = 0; copy < 2; copy++) {
        uint64_t lba = 2048 + fs + (copy ? backup : 0);
        if (!r.read(r.context, lba, fsinfo)) return 0;
        if (copy && zu_empty(fsinfo)) continue;
        if (zeal_partition_u32(fsinfo) != 0x41615252u ||
            zeal_partition_u32(fsinfo + 484) != 0x61417272u ||
            zeal_partition_u32(fsinfo + 508) != 0xaa550000u) return 0;
        zu_put32(fsinfo + 488, UINT32_MAX); zu_put32(fsinfo + 492, UINT32_MAX);
        if (!zu_journal_write(j, lba, fsinfo)) return 0;
    }
    efi_reclaim_encode(b + c.offset, cluster, c.remaining);
    if (!zu_journal_write(j, c.lba, b) || !zu_journal_commit(j)) return 0;
    *done = 0; return 1;
}
