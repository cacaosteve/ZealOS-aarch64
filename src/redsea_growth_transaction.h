/* Experimental RedSea directory relocation inside an already-owned undo
 * transaction. This is not connected to the guest. The caller must hold the
 * filesystem lock, recover and own an isolated journal, and abort/recover the
 * transaction after any failure. LBAs are physical and all extents must lie
 * within the journal's allowed range. */
#pragma once
#include "redsea_growth_ownership.h"

static uint64_t rsg_u64(const uint8_t *p) { return zeal_partition_u64(p); }
static void rsg_u64put(uint8_t *p, uint64_t v) { zu_put64(p, v); }
static int rsg_read(struct zu_journal *j, uint64_t lba, uint8_t b[512]) {
    return lba >= j->allowed_first && lba < j->allowed_end &&
           j->io.read(j->io.context, lba, b);
}
static int rsg_bit(struct zu_journal *j, uint64_t base, uint64_t sectors,
                   uint64_t maps, uint64_t sector, int *set) {
    uint8_t b[512];
    if (maps >= sectors || sector < base + maps || sector - base >= sectors) return 0;
    uint64_t bit = sector - base - maps, map = bit / 4096;
    if (map >= maps || !rsg_read(j, base + 1 + map, b)) return 0;
    *set = !!(b[(bit % 4096) / 8] & (1u << (bit % 8)));
    return 1;
}
static int rsg_mark(struct zu_journal *j, uint64_t base, uint64_t maps,
                    uint64_t sector, int set) {
    uint8_t b[512]; uint64_t bit = sector - base - maps;
    uint64_t lba = base + 1 + bit / 4096;
    if (!rsg_read(j, lba, b)) return 0;
    uint8_t mask = (uint8_t)(1u << (bit % 8));
    if (set) b[(bit % 4096) / 8] |= mask;
    else b[(bit % 4096) / 8] &= (uint8_t)~mask;
    return zu_journal_write(j, lba, b);
}
/* Move a full directory into a disjoint, free contiguous extent. The root is
 * identified by parent_sector == base; for a non-root directory parent_sector
 * and parent_offset locate its parent entry. Entries, dot and child dot-dot
 * links are validated before mutation. A transaction may fail on journal
 * capacity; its caller must roll back before allowing any other operation. */
static int rsg_grow(struct zu_journal *j, uint64_t base, uint64_t sectors,
                    uint64_t maps, uint64_t bias, uint64_t old, uint64_t fresh,
                    uint64_t parent_sector, unsigned parent_offset,
                    struct rsg_ownership *owner) {
    uint8_t b[512], h[512]; int bit;
    if (!j || !sectors || base < bias || base < j->allowed_first || base >= j->allowed_end ||
        sectors > j->allowed_end - base || !maps || maps >= sectors ||
        parent_offset > 448 || (parent_offset & 63) ||
        old < base + maps || old - base >= sectors ||
        fresh < base + maps || fresh - base >= sectors ||
        !rsg_read(j, old, h) || h[2] != '.' || h[3] ||
        !(h[0] & 0x10) || !(h[1] & 4) || (h[1] & 1) ||
        rsg_u64(h + 40) != old - bias || h[66] != '.' || h[67] != '.' || h[68]) return 0;
    uint64_t length = rsg_u64(h + 48), blocks = length / 512;
    uint64_t parent_raw = rsg_u64(h + 104);
    if (parent_raw > UINT64_MAX - bias) return 0;
    uint64_t parent_dir = parent_raw + bias;
    if (!length || length > UINT64_MAX / 2 || (length & 511) || blocks > (sectors - (old - base)) ||
        blocks > (sectors - (fresh - base)) / 2 || blocks > UINT64_MAX / 2 ||
        (fresh < old + blocks && old < fresh + blocks * 2)) return 0;
    for (uint64_t i = 0; i < blocks; i++)
        if (!rsg_bit(j, base, sectors, maps, old + i, &bit) || !bit) return 0;
    for (uint64_t i = 0; i < blocks * 2; i++)
        if (!rsg_bit(j, base, sectors, maps, fresh + i, &bit) || bit) return 0;
    if (!rsg_read(j, parent_sector, b)) return 0;
    if (parent_sector == base) {
        if (parent_offset || parent_dir != old || rsg_u64(b + 24) != old - bias) return 0;
    } else {
        if (parent_dir < base + maps || parent_dir - base >= sectors ||
            parent_dir == old || parent_sector < parent_dir ||
            !rsg_read(j, parent_dir, h) || h[2] != '.' || h[3] ||
            rsg_u64(h + 40) != parent_dir - bias || !rsg_u64(h + 48) ||
            (rsg_u64(h + 48) & 511) ||
            (parent_sector - parent_dir) >= rsg_u64(h + 48) / 512 ||
            !(b[parent_offset] & 0x10) || (b[parent_offset + 1] & 1) ||
            rsg_u64(b + parent_offset + 40) != old - bias ||
            rsg_u64(b + parent_offset + 48) != length) return 0;
    }
    /* Reject aliases and cycles across the entire volume before freeing the
     * old extent. Hold the same lock through this scan and the transaction. */
    if (!rsg_ownership_check(j, base, sectors, maps, bias, owner)) return 0;
    /* Validate children before changing any metadata. */
    for (uint64_t n = 0; n < blocks; n++) {
        if (!rsg_read(j, old + n, b)) return 0;
        for (unsigned k = 0; k < 8; k++) {
            const uint8_t *e = b + 64*k;
            if (!e[2]) break;
            if (!(e[0] & 0x10) || (e[1] & 1) ||
                (e[2] == '.' && (!e[3] || (e[3] == '.' && !e[4])))) continue;
            uint64_t raw = rsg_u64(e + 40);
            if (raw > UINT64_MAX - bias) return 0;
            uint64_t child = raw + bias;
            if (child < base + maps || child - base >= sectors ||
                (child >= old && child < old + blocks) ||
                (child >= fresh && child < fresh + 2*blocks) ||
                !rsg_bit(j, base, sectors, maps, child, &bit) || !bit ||
                !rsg_read(j, child, h) || h[2] != '.' || h[3] ||
                rsg_u64(h + 40) != child - bias || h[66] != '.' || h[67] != '.' || h[68] ||
                rsg_u64(h + 104) != old - bias) return 0;
        }
    }
    for (uint64_t i = 0; i < blocks * 2; i++)
        if (!rsg_mark(j, base, maps, fresh + i, 1)) return 0;
    for (uint64_t i = 0; i < blocks * 2; i++) {
        if (i < blocks) { if (!rsg_read(j, old + i, b)) return 0; }
        else for (unsigned k = 0; k < 512; k++) b[k] = 0;
        if (!i) {
            rsg_u64put(b + 40, fresh - bias); rsg_u64put(b + 48, length * 2);
            if (parent_sector == base) rsg_u64put(b + 104, fresh - bias);
        }
        if (!zu_journal_write(j, fresh + i, b)) return 0;
    }
    for (uint64_t n = 0; n < blocks; n++) {
        if (!rsg_read(j, old + n, b)) return 0;
        for (unsigned k = 0; k < 8; k++) {
            const uint8_t *e = b + 64*k;
            if (!e[2]) break;
            if (!(e[0] & 0x10) || (e[1] & 1) ||
                (e[2] == '.' && (!e[3] || (e[3] == '.' && !e[4])))) continue;
            uint64_t child = rsg_u64(e + 40) + bias; /* validated above */
            if (!rsg_read(j, child, h)) return 0;
            rsg_u64put(h + 104, fresh - bias);
            if (!zu_journal_write(j, child, h)) return 0;
        }
    }
    if (!rsg_read(j, parent_sector, b)) return 0;
    if (parent_sector == base) rsg_u64put(b + 24, fresh - bias);
    else { rsg_u64put(b + parent_offset + 40, fresh - bias); rsg_u64put(b + parent_offset + 48, length * 2); }
    if (!zu_journal_write(j, parent_sector, b)) return 0;
    for (uint64_t i = 0; i < blocks; i++)
        if (!rsg_mark(j, base, maps, old + i, 0)) return 0;
    return 1;
}
