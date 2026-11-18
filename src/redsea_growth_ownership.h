/* Read-only, bounded RedSea allocation ownership check for the experimental
 * directory-growth path. Unreferenced allocations are tolerated but are not
 * treated as free. No mount or writer calls this checker yet. */
#pragma once
#include "upgrade_journal.h"
#define RSG_OWNER_MAX_SECTORS (1u << 22)
#define RSG_OWNER_MAX_DIRS 4096u
struct rsg_owner_dir { uint64_t first, bytes, parent; };
struct rsg_ownership {
    uint8_t used[(RSG_OWNER_MAX_SECTORS + 7u) / 8u];
    struct rsg_owner_dir dirs[RSG_OWNER_MAX_DIRS];
    uint32_t count;
};
static int rsg_owner_claim(struct zu_journal *j, struct rsg_ownership *o,
                           uint64_t base, uint64_t sectors, uint64_t maps,
                           uint64_t first, uint64_t blocks) {
    uint8_t b[512]; uint64_t cached = UINT64_MAX;
    if (!blocks || first < base + maps || first - base >= sectors ||
        blocks > sectors - (first - base)) return 0;
    for (uint64_t i = 0; i < blocks; i++) {
        uint64_t bit = first + i - base - maps, map = bit / 4096;
        if (map >= maps || bit >= RSG_OWNER_MAX_SECTORS ||
            (o->used[bit / 8] & (uint8_t)(1u << (bit % 8)))) return 0;
        if (cached != map) {
            if (!j->io.read(j->io.context, base + 1 + map, b)) return 0;
            cached = map;
        }
        if (!(b[(bit % 4096) / 8] & (uint8_t)(1u << (bit % 8)))) return 0;
        o->used[bit / 8] |= (uint8_t)(1u << (bit % 8));
    }
    return 1;
}
static int rsg_ownership_check(struct zu_journal *j, uint64_t base,
                               uint64_t sectors, uint64_t maps, uint64_t bias,
                               struct rsg_ownership *o) {
    uint8_t boot[512], block[512];
    if (!j || !o || !j->io.read || !sectors || sectors > RSG_OWNER_MAX_SECTORS ||
        base < bias || base < j->allowed_first || base >= j->allowed_end ||
        sectors > j->allowed_end - base || !maps || maps >= sectors ||
        maps < (sectors + 4095) / 4096 ||
        !j->io.read(j->io.context, base, boot) || boot[3] != 0x88 ||
        boot[510] != 0x55 || boot[511] != 0xaa ||
        zeal_partition_u64(boot + 8) != base - bias ||
        zeal_partition_u64(boot + 16) != sectors ||
        zeal_partition_u64(boot + 32) != maps) return 0;
    for (unsigned i = 0; i < sizeof(o->used); i++) o->used[i] = 0;
    uint64_t root_raw = zeal_partition_u64(boot + 24);
    if (root_raw > UINT64_MAX - bias) return 0;
    uint64_t root = root_raw + bias;
    if (root < base + maps || root - base >= sectors ||
        !j->io.read(j->io.context, root, block)) return 0;
    uint64_t len = zeal_partition_u64(block + 48);
    if (!len || (len & 511)) return 0;
    o->count = 1; o->dirs[0] = (struct rsg_owner_dir){root,len,root};
    for (uint32_t d = 0; d < o->count; d++) {
        struct rsg_owner_dir dir = o->dirs[d];
        if (!dir.bytes || (dir.bytes & 511) ||
            !rsg_owner_claim(j,o,base,sectors,maps,dir.first,dir.bytes/512) ||
            !j->io.read(j->io.context,dir.first,block) ||
            block[0] != 0x10 || block[1] != 4 || block[2] != '.' || block[3] ||
            zeal_partition_u64(block+40) != dir.first-bias || zeal_partition_u64(block+48) != dir.bytes ||
            block[64] != 0x10 || block[65] != 4 || block[66] != '.' || block[67] != '.' || block[68] ||
            zeal_partition_u64(block+104) != dir.parent-bias) return 0;
        int ended = 0;
        for (uint64_t n=0; n<dir.bytes/512 && !ended; n++) {
            if (n && !j->io.read(j->io.context,dir.first+n,block)) return 0;
            for (unsigned k=0;k<8;k++) {
                const uint8_t *e=block+64*k; uint16_t a=(uint16_t)e[0]|((uint16_t)e[1]<<8);
                if (!e[2]) { ended=1; break; }
                if (a & 0x100u) continue;
                if (n==0 && k<2) continue;
                if (!(a & 0x400u)) return 0;
                unsigned name_len=0;
                while (name_len<38 && e[2+name_len]) {
                    uint8_t c=e[2+name_len++];
                    if(c<32 || c>126 || c=='/' || c=='\\' || c==':')return 0;
                }
                if (!name_len || name_len>=38 || (e[2]=='.' && (!e[3] || (e[3]=='.' && !e[4])))) return 0;
                uint64_t raw=zeal_partition_u64(e+40), bytes=zeal_partition_u64(e+48);
                if (bytes && raw > UINT64_MAX-bias) return 0;
                uint64_t first=bytes ? raw+bias : raw;
                if (a & 0x10u) {
                    if (!bytes || (bytes & 511) || o->count==RSG_OWNER_MAX_DIRS) return 0;
                    o->dirs[o->count++] = (struct rsg_owner_dir){first,bytes,dir.first};
                } else if (bytes) {
                    if (bytes>UINT64_MAX-511 || !rsg_owner_claim(j,o,base,sectors,maps,first,(bytes+511)/512)) return 0;
                } else if (first != UINT64_MAX && first != 0) return 0;
            }
        }
    }
    return 1;
}
