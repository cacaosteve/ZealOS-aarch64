/* Persistent ownership of the reserved GPT-gap upgrade journal. */
#pragma once
#include "source_upgrade_pending.h"
#define ZU_OWNER_LBA 1990ull
#define ZU_OWNER_SHADOW_LBA 1991ull
static void zu_owner_encode(uint8_t b[512], const uint8_t gpt[512]) {
    for (unsigned i = 0; i < 512; i++) b[i] = 0;
    const char *magic = "ZUOWNER1";
    for (unsigned i = 0; i < 8; i++) b[i] = (uint8_t)magic[i];
    for (unsigned i = 0; i < 16; i++) b[8 + i] = gpt[56 + i];
    zu_put32(b + 24, zeal_partition_crc(b, 24));
}
/* A torn first publication into a previously empty sector has an exact
 * prefix of the intended record followed entirely by zeroes. */
static int zu_owner_partial(const uint8_t b[512], const uint8_t expected[512]) {
    int suffix = 0;
    for (unsigned i = 0; i < 512; i++) {
        if (!suffix && b[i] != expected[i]) suffix = 1;
        if (suffix && b[i]) return 0;
    }
    return 1;
}
/* Existing owner records bind to this disk's GPT identifier. If absent, a
 * committed prior update and a completely empty tail are required before
 * claiming it. Headers must be retired; no live journal may be overwritten. */
static int zu_owner_prepare(struct zu_journal *j, int allow_create) {
    uint8_t gpt[512], expected[512], primary[512], shadow[512], b[512];
    if (!j || !zu_valid_io(&j->io) || j->io.journal != 34 || j->slots != ZU_JOURNAL_MAX_SLOTS ||
        !j->io.read(j->io.context, 1, gpt) || !j->io.read(j->io.context, 34, b) || !zu_empty(b) ||
        !j->io.read(j->io.context, 34 + 2 + 3ull*j->slots, b) || !zu_empty(b)) return 0;
    zu_owner_encode(expected, gpt);
    if (!j->io.read(j->io.context, ZU_OWNER_LBA, primary) ||
        !j->io.read(j->io.context, ZU_OWNER_SHADOW_LBA, shadow)) return 0;
    int primary_ok = 1, shadow_ok = 1;
    for (unsigned i = 0; i < 512; i++) {
        if (primary[i] != expected[i]) primary_ok = 0;
        if (shadow[i] != expected[i]) shadow_ok = 0;
    }
    if ((!primary_ok && !zu_owner_partial(primary, expected)) ||
        (!shadow_ok && !zu_owner_partial(shadow, expected))) return 0;
    if (!primary_ok && !shadow_ok && (!allow_create || zu_journal_committed(j) != 1)) return 0;
    for (uint64_t lba = 1987; lba < 2048; lba++) {
        if (lba == ZU_PENDING_PRIMARY || lba == ZU_PENDING_SHADOW ||
            lba == ZU_OWNER_LBA || lba == ZU_OWNER_SHADOW_LBA) continue;
        if (lba >= 1992 && lba <= 1995 && (primary_ok || shadow_ok)) continue;
        if (!j->io.read(j->io.context, lba, b) || !zu_empty(b)) return 0;
    }
    const uint64_t copies[2] = {ZU_OWNER_SHADOW_LBA, ZU_OWNER_LBA};
    const int valid[2] = {shadow_ok, primary_ok};
    for (unsigned n = 0; n < 2; n++) if (!valid[n]) {
        if (!j->io.write(j->io.context, copies[n], expected) || !j->io.flush(j->io.context) ||
            !j->io.read(j->io.context, copies[n], b)) return 0;
        for (unsigned i = 0; i < 512; i++) if (b[i] != expected[i]) return 0;
    }
    return 1;
}
/* The old commit must be cleared durably before a new source marker can be
 * published; otherwise the boot path could mistake an old commit for a new one. */
static int zu_owner_clear_commit(struct zu_journal *j) {
    uint8_t zero[512], b[512];
    for (unsigned i = 0; i < 512; i++) zero[i] = 0;
    uint64_t lba = j->io.journal + 1 + 3ull*j->slots;
    return j->io.write(j->io.context, lba, zero) && j->io.flush(j->io.context) &&
           j->io.read(j->io.context, lba, b) && zu_empty(b);
}
/* If the first pending-marker publication tears, its primary is still empty.
 * With verified gap ownership, retired journal headers and no commit, no EFI
 * switch can have started; discard only the recognizable partial shadow. */
static int zu_owner_recover_torn_pending(struct zu_journal *j) {
    uint8_t primary[512], shadow[512], zero[512], check[512];
    const char *magic = "ZUPEND01";
    if (!zu_owner_prepare(j, 0) || zu_journal_committed(j) != 0 ||
        !j->io.read(j->io.context, ZU_PENDING_PRIMARY, primary) || !zu_empty(primary) ||
        !j->io.read(j->io.context, ZU_PENDING_SHADOW, shadow) || zu_empty(shadow)) return 0;
    unsigned matched = 0;
    while (matched < 8 && shadow[matched] == (uint8_t)magic[matched]) matched++;
    if (!matched) return 0;
    if (matched < 8) for (unsigned i = matched; i < 512; i++) if (shadow[i]) return 0;
    for (unsigned i = 0; i < 512; i++) zero[i] = 0;
    return j->io.write(j->io.context, ZU_PENDING_SHADOW, zero) && j->io.flush(j->io.context) &&
           j->io.read(j->io.context, ZU_PENDING_SHADOW, check) && zu_empty(check);
}
