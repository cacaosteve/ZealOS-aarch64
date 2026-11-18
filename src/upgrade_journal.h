/* Experimental write-ahead undo journal. Not connected to the guest.
 * The caller owns an exclusive, previously reserved journal range and must
 * arrange for recovery before mounting the filesystem. Durable flushes must
 * order preceding writes. Invalid journal records are rejected, never guessed. */
#pragma once
#include "upgrade_commit.h"
#define ZU_JOURNAL_MAX_SLOTS 650u

struct zu_journal {
    struct zu_commit_io io;
    uint64_t allowed_first, allowed_end;
    uint32_t slots, next;
};

static int zu_journal_header_ok(const struct zu_journal *j, const uint8_t h[512]) {
    const char *magic = "ZUJRNL02";
    for (unsigned i = 0; i < 8; i++) if (h[i] != (uint8_t)magic[i]) return 0;
    return zeal_partition_u32(h + 8) == j->slots &&
           zeal_partition_u64(h + 12) == j->allowed_first &&
           zeal_partition_u64(h + 20) == j->allowed_end &&
           zeal_partition_u32(h + 28) == zeal_partition_crc(h, 28);
}

/* Check the durable commit record even after both journal headers have been
 * retired. The caller must establish that this journal was empty before the
 * transaction, so a record from an older transaction cannot be mistaken for
 * the current one. Returns -1 on read error, 0 if absent/invalid, 1 if valid. */
static int zu_journal_committed(struct zu_journal *j) {
    uint8_t h[512] = {0}, record[512];
    if (!j || !zu_valid_io(&j->io) || !j->slots || j->slots > ZU_JOURNAL_MAX_SLOTS ||
        3ull + 3ull*j->slots > j->io.capacity - j->io.journal ||
        !j->io.read(j->io.context, j->io.journal + 1 + 3ull*j->slots, record)) return -1;
    const char *magic = "ZUJRNL02", *done = "ZUDONE01";
    for (unsigned i = 0; i < 8; i++) { h[i] = (uint8_t)magic[i]; if (record[i] != (uint8_t)done[i]) return 0; }
    zu_put32(h + 8, j->slots); zu_put64(h + 12, j->allowed_first);
    zu_put64(h + 20, j->allowed_end); zu_put32(h + 28, zeal_partition_crc(h, 28));
    return zeal_partition_u32(record + 8) == zeal_partition_crc(h, 512) &&
           zeal_partition_u32(record + 12) == zeal_partition_crc(record, 12);
}

/* Retire one header at a time; the other remains usable if clearing tears. */
static int zu_journal_retire(struct zu_journal *j) {
    volatile uint8_t zero[512];
    uint8_t check[512];
    for (unsigned i = 0; i < 512; i++) zero[i] = 0;
    uint64_t shadow = j->io.journal + 2 + 3ull * j->slots;
    if (!j->io.write(j->io.context, j->io.journal, (const uint8_t *)zero) || !j->io.flush(j->io.context) ||
        !j->io.read(j->io.context, j->io.journal, check) || !zu_empty(check)) return 0;
    return j->io.write(j->io.context, shadow, (const uint8_t *)zero) && j->io.flush(j->io.context) &&
           j->io.read(j->io.context, shadow, check) && zu_empty(check);
}

static int zu_journal_begin(struct zu_journal *j) {
    uint8_t b[512] = {0}, check[512];
    if (!j || !zu_valid_io(&j->io) || !j->slots || j->slots > ZU_JOURNAL_MAX_SLOTS ||
        j->allowed_first >= j->allowed_end || j->allowed_end > j->io.capacity ||
        3ull + 3ull*j->slots > j->io.capacity - j->io.journal ||
        (j->allowed_first < j->io.journal + 3ull + 3ull*j->slots &&
         j->io.journal < j->allowed_end) ||
        !j->io.read(j->io.context, j->io.journal, check) || !zu_empty(check) ||
        !j->io.read(j->io.context, j->io.journal + 2 + 3ull*j->slots, check) ||
        !zu_empty(check)) return 0;
    /* Erase old descriptors before publishing the active marker. */
    for (uint32_t i = 0; i < j->slots; i++) {
        if (!j->io.write(j->io.context, j->io.journal + 1 + 3ull*i, b) ||
            !j->io.write(j->io.context, j->io.journal + 2 + 3ull*i, b)) return 0;
    }
    if (!j->io.write(j->io.context, j->io.journal + 1 + 3ull*j->slots, b)) return 0;
    if (!j->io.flush(j->io.context)) return 0;
    const char *magic = "ZUJRNL02";
    for (unsigned i = 0; i < 8; i++) b[i] = (uint8_t)magic[i];
    zu_put32(b + 8, j->slots);
    zu_put64(b + 12, j->allowed_first); zu_put64(b + 20, j->allowed_end);
    zu_put32(b + 28, zeal_partition_crc(b, 28));
    uint64_t shadow = j->io.journal + 2 + 3ull*j->slots;
    if (!j->io.write(j->io.context, shadow, b) || !j->io.flush(j->io.context) ||
        !j->io.read(j->io.context, shadow, check)) return 0;
    for (unsigned i = 0; i < 512; i++) if (b[i] != check[i]) return 0;
    if (!j->io.write(j->io.context, j->io.journal, b) || !j->io.flush(j->io.context) ||
        !j->io.read(j->io.context, j->io.journal, check)) return 0;
    for (unsigned i = 0; i < 512; i++) if (b[i] != check[i]) return 0;
    j->next = 0;
    return 1;
}

/* Publish a separate checksummed commit record before retiring the undo log.
 * Once that record is durable, recovery keeps the new metadata. */
static int zu_journal_commit(struct zu_journal *j) {
    uint8_t active[512], done[512] = {0}, check[512];
    if (!j || !zu_valid_io(&j->io) || !j->slots || j->slots > ZU_JOURNAL_MAX_SLOTS ||
        3ull + 3ull*j->slots > j->io.capacity - j->io.journal ||
        !j->io.read(j->io.context, j->io.journal, active)) return 0;
    const char *magic = "ZUJRNL02";
    for (unsigned i = 0; i < 8; i++) if (active[i] != (uint8_t)magic[i]) return 0;
    if (zeal_partition_u32(active + 8) != j->slots ||
        zeal_partition_u64(active + 12) != j->allowed_first ||
        zeal_partition_u64(active + 20) != j->allowed_end ||
        zeal_partition_u32(active + 28) != zeal_partition_crc(active, 28)) return 0;
    const char *done_magic = "ZUDONE01";
    for (unsigned i = 0; i < 8; i++) done[i] = (uint8_t)done_magic[i];
    zu_put32(done + 8, zeal_partition_crc(active, 512));
    zu_put32(done + 12, zeal_partition_crc(done, 12));
    uint64_t pos = j->io.journal + 1 + 3ull*j->slots;
    if (!j->io.flush(j->io.context) || !j->io.write(j->io.context, pos, done) ||
        !j->io.flush(j->io.context) || !j->io.read(j->io.context, pos, check)) return 0;
    for (unsigned i = 0; i < 512; i++) if (done[i] != check[i]) return 0;
    return zu_journal_retire(j);
}

static int zu_journal_write(struct zu_journal *j, uint64_t target,
                            const uint8_t replacement[512]) {
    uint8_t old[512], hdr[512] = {0}, check[512];
    if (!j || !replacement || j->next >= j->slots || target < j->allowed_first ||
        target >= j->allowed_end || !j->io.read(j->io.context, target, old)) return 0;
    uint64_t pos = j->io.journal + 1 + 3ull*j->next;
    const char *magic = "ZUOLD001";
    for (unsigned i = 0; i < 8; i++) hdr[i] = (uint8_t)magic[i];
    zu_put64(hdr + 8, target); zu_put32(hdr + 16, zeal_partition_crc(old, 512));
    zu_put32(hdr + 20, zeal_partition_crc(hdr, 20));
    if (!j->io.write(j->io.context, pos + 2, old) || !j->io.flush(j->io.context) ||
        !j->io.read(j->io.context, pos + 2, check)) return 0;
    for (unsigned i = 0; i < 512; i++) if (check[i] != old[i]) return 0;
    if (!j->io.write(j->io.context, pos + 1, hdr) || !j->io.flush(j->io.context) ||
        !j->io.read(j->io.context, pos + 1, check)) return 0;
    for (unsigned i = 0; i < 512; i++) if (check[i] != hdr[i]) return 0;
    if (!j->io.write(j->io.context, pos, hdr) || !j->io.flush(j->io.context) ||
        !j->io.read(j->io.context, pos, check)) return 0;
    for (unsigned i = 0; i < 512; i++) if (check[i] != hdr[i]) return 0;
    j->next++;
    if (!j->io.write(j->io.context, target, replacement) || !j->io.flush(j->io.context) ||
        !j->io.read(j->io.context, target, check)) return 0;
    for (unsigned i = 0; i < 512; i++) if (check[i] != replacement[i]) return 0;
    return 1;
}

static int zu_journal_record_ok(const struct zu_journal *j, const uint8_t *r) {
    const char *magic = "ZUOLD001";
    for (unsigned i = 0; i < 8; i++) if (r[i] != (uint8_t)magic[i]) return 0;
    uint64_t lba = zeal_partition_u64(r + 8);
    return lba >= j->allowed_first && lba < j->allowed_end &&
           zeal_partition_u32(r + 20) == zeal_partition_crc(r, 20);
}

/* Return 1 for a valid descriptor, 0 for an unused tail, -1 on damage.
 * A partial shadow with an empty primary is an unfinished final publication:
 * the target cannot have been written because primary publication follows it. */
static int zu_journal_read_record(struct zu_journal *j, uint32_t index, uint8_t out[512]) {
    uint8_t a[512], b[512];
    uint64_t pos = j->io.journal + 1 + 3ull*index;
    if (!j->io.read(j->io.context, pos, a) || !j->io.read(j->io.context, pos + 1, b)) return -1;
    int av = zu_journal_record_ok(j, a), bv = zu_journal_record_ok(j, b);
    if (!av && !bv) {
        if (!zu_empty(a)) return -1;
        for (uint32_t i = index + 1; i < j->slots; i++) {
            if (!j->io.read(j->io.context, j->io.journal + 1 + 3ull*i, a) ||
                !zu_empty(a) || !j->io.read(j->io.context, j->io.journal + 2 + 3ull*i, a) ||
                !zu_empty(a)) return -1;
        }
        return 0;
    }
    if (av && bv) for (unsigned i = 0; i < 512; i++) if (a[i] != b[i]) return -1;
    for (unsigned i = 0; i < 512; i++) out[i] = av ? a[i] : b[i];
    return 1;
}

/* Recovery always rolls back. Call only before the filesystem is used. */
static int zu_journal_recover(struct zu_journal *j) {
    uint8_t hdr[512], shadow[512], record[512], data[512], check[512];
    if (!j || !zu_valid_io(&j->io) || !j->slots || j->slots > ZU_JOURNAL_MAX_SLOTS ||
        3ull + 3ull*j->slots > j->io.capacity - j->io.journal ||
        !j->io.read(j->io.context, j->io.journal, hdr) ||
        !j->io.read(j->io.context, j->io.journal + 2 + 3ull*j->slots, shadow)) return 0;
    if (zu_empty(hdr) && zu_empty(shadow)) return 1;
    int primary_ok = zu_journal_header_ok(j, hdr), shadow_ok = zu_journal_header_ok(j, shadow);
    if (!primary_ok && !shadow_ok) {
        /* Under the single interrupted-write model, a torn first shadow
         * publication precedes all target writes. Likewise, retirement
         * clears the primary only after rollback or durable commit. Do not
         * infer safety when the primary is nonempty but invalid. */
        if (!zu_empty(hdr)) return 0;
        volatile uint8_t zero[512];
        for (unsigned i = 0; i < 512; i++) zero[i] = 0;
        uint64_t lba = j->io.journal + 2 + 3ull*j->slots;
        return j->io.write(j->io.context, lba, (const uint8_t *)zero) && j->io.flush(j->io.context) &&
               j->io.read(j->io.context, lba, check) && zu_empty(check);
    }
    if (!primary_ok) for (unsigned i = 0; i < 512; i++) hdr[i] = shadow[i];
    if (primary_ok && shadow_ok)
        for (unsigned i = 0; i < 512; i++) if (hdr[i] != shadow[i]) return 0;
    uint32_t slots = zeal_partition_u32(hdr + 8);
    uint64_t first = zeal_partition_u64(hdr + 12), end = zeal_partition_u64(hdr + 20);
    if (!slots || slots > ZU_JOURNAL_MAX_SLOTS || first >= end || end > j->io.capacity ||
        (first < j->io.journal + 3ull + 3ull*slots && j->io.journal < end) ||
        first != j->allowed_first || end != j->allowed_end ||
        slots != j->slots || 3ull + 3ull*slots > j->io.capacity - j->io.journal ||
        zeal_partition_u32(hdr + 28) != zeal_partition_crc(hdr, 28)) return 0;
    if (!j->io.read(j->io.context, j->io.journal + 1 + 3ull*slots, record)) return 0;
    const char *done_magic = "ZUDONE01";
    int committed = 1;
    for (unsigned i = 0; i < 8; i++) if (record[i] != (uint8_t)done_magic[i]) committed = 0;
    if (committed && zeal_partition_u32(record + 8) == zeal_partition_crc(hdr, 512) &&
        zeal_partition_u32(record + 12) == zeal_partition_crc(record, 12))
        return zu_journal_retire(j);
    uint32_t count = 0;
    for (; count < slots; count++) {
        int status = zu_journal_read_record(j, count, record);
        if (status < 0) return 0;
        if (!status) break;
        uint64_t lba = zeal_partition_u64(record + 8);
        if (lba < first || lba >= end ||
            !j->io.read(j->io.context, j->io.journal + 3 + 3ull*count, data) ||
            zeal_partition_u32(record + 16) != zeal_partition_crc(data, 512)) return 0;
    }
    /* Reverse order also handles repeated writes to the same sector. */
    while (count) {
        count--;
        uint64_t pos = j->io.journal + 1 + 3ull*count;
        if (zu_journal_read_record(j, count, record) != 1 ||
            !j->io.read(j->io.context, pos + 2, data)) return 0;
        uint64_t lba = zeal_partition_u64(record + 8);
        if (!j->io.write(j->io.context, lba, data) || !j->io.flush(j->io.context) ||
            !j->io.read(j->io.context, lba, check)) return 0;
        for (unsigned i = 0; i < 512; i++) if (check[i] != data[i]) return 0;
    }
    return zu_journal_retire(j);
}
