/* Fingerprints of updater-owned EFI slot files, kept in the reserved GPT gap.
 * A slot is reclaimable only when every file still matches its recorded bytes. */
#pragma once
#include "efi_upgrade_owner.h"
#include "efi_fat32_read.h"
#define ZU_SLOT_U_PRIMARY 1992ull
#define ZU_SLOT_U_SHADOW 1993ull
#define ZU_SLOT_V_PRIMARY 1994ull
#define ZU_SLOT_V_SHADOW 1995ull
struct zu_slot_fingerprint { uint32_t bytes[3], crc[3]; };
static const uint8_t zu_slot_names[2][3][11] = {
    {{'Z','U','S','O','U','R','C','E','L','Z','4'}, {'Z','U','K','E','R','N','E','L','E','L','F'},
     {'Z','U','B','O','O','T','E','R','E','L','F'}},
    {{'Z','V','S','O','U','R','C','E','L','Z','4'}, {'Z','V','K','E','R','N','E','L','E','L','F'},
     {'Z','V','B','O','O','T','E','R','E','L','F'}}};
static int zu_slot_file_crc(struct efi_source_reader *r, const uint8_t name[11], uint32_t *size, uint32_t *crc) {
    uint8_t b[512]; uint32_t cluster, bytes, next, sum = 0xffffffffu;
    if (!efi_source_find(r, r->root, name, 0, &cluster, &bytes) || !bytes) return 0;
    *size = bytes; uint32_t left = bytes;
    while (left) {
        if (cluster < 2 || cluster > r->space.clusters + 1 || !efi_source_next_cluster(r, cluster, &next)) return 0;
        uint64_t lba = r->space.data_first + (uint64_t)(cluster - 2) * r->space.sectors_per_cluster;
        for (uint32_t i = 0; i < r->space.sectors_per_cluster && left; i++) {
            if (!r->read(r->context, lba + i, b)) return 0;
            uint32_t n = left > 512 ? 512 : left;
            for (uint32_t k = 0; k < n; k++) sum = zeal_partition_crc_step(sum, b[k]);
            left -= n;
        }
        if (left) { if (next < 2 || next > r->space.clusters + 1) return 0; cluster = next; }
        else if (next < 0x0ffffff8u) return 0;
    }
    *crc = sum ^ 0xffffffffu; return 1;
}
static int zu_slot_fingerprint_read(struct efi_source_reader *r, unsigned slot, struct zu_slot_fingerprint *out) {
    if (!r || !out || slot > 1) return 0;
    for (unsigned i = 0; i < 3; i++)
        if (!zu_slot_file_crc(r, zu_slot_names[slot][i], &out->bytes[i], &out->crc[i])) return 0;
    return 1;
}
static void zu_slot_record(uint8_t b[512], const uint8_t gpt[512], unsigned slot,
                           const struct zu_slot_fingerprint *f) {
    for (unsigned i = 0; i < 512; i++) b[i] = 0;
    const char *magic = "ZUSLOT01";
    for (unsigned i = 0; i < 8; i++) b[i] = (uint8_t)magic[i];
    for (unsigned i = 0; i < 16; i++) b[8+i] = gpt[56+i];
    zu_put32(b + 24, slot);
    for (unsigned i = 0; i < 3; i++) { zu_put32(b + 28 + 4*i, f->bytes[i]); zu_put32(b + 40 + 4*i, f->crc[i]); }
    zu_put32(b + 52, zeal_partition_crc(b, 52));
}
/* 1=valid identical copies, 0=both empty, -1=damaged or ambiguous. */
static int zu_slot_manifest_read(struct zu_journal *j, unsigned slot, struct zu_slot_fingerprint *out) {
    uint8_t a[512], b[512], gpt[512];
    if (!j || !out || slot > 1 || !j->io.read(j->io.context, 1, gpt)) return -1;
    uint64_t lba = slot ? ZU_SLOT_V_PRIMARY : ZU_SLOT_U_PRIMARY;
    if (!j->io.read(j->io.context, lba, a) || !j->io.read(j->io.context, lba + 1, b)) return -1;
    if (zu_empty(a) && zu_empty(b)) return 0;
    if (zu_empty(a) || zu_empty(b)) return -1;
    for (unsigned i = 0; i < 512; i++) if (a[i] != b[i]) return -1;
    const char *magic = "ZUSLOT01";
    for (unsigned i = 0; i < 8; i++) if (a[i] != (uint8_t)magic[i]) return -1;
    for (unsigned i = 0; i < 16; i++) if (a[8+i] != gpt[56+i]) return -1;
    if (zeal_partition_u32(a + 24) != slot || zeal_partition_u32(a + 52) != zeal_partition_crc(a, 52)) return -1;
    for (unsigned i = 0; i < 3; i++) { out->bytes[i] = zeal_partition_u32(a + 28 + 4*i); out->crc[i] = zeal_partition_u32(a + 40 + 4*i); }
    return 1;
}
/* Caller holds ownership of the gap and may only replace an inactive slot's
 * manifest after the corresponding old slot has been removed. */
static int zu_slot_manifest_write(struct zu_journal *j, unsigned slot, const struct zu_slot_fingerprint *f) {
    uint8_t gpt[512], expected[512], b[512];
    if (!j || !f || slot > 1 || !j->io.read(j->io.context, 1, gpt)) return 0;
    zu_slot_record(expected, gpt, slot, f);
    uint64_t first = slot ? ZU_SLOT_V_PRIMARY : ZU_SLOT_U_PRIMARY;
    for (int i = 1; i >= 0; i--) {
        if (!j->io.write(j->io.context, first + (uint64_t)i, expected) || !j->io.flush(j->io.context) ||
            !j->io.read(j->io.context, first + (uint64_t)i, b)) return 0;
        for (unsigned k = 0; k < 512; k++) if (b[k] != expected[k]) return 0;
    }
    return 1;
}
