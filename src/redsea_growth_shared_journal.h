/* Experimental coordination for borrowing the EFI undo journal. Not wired to
 * guest startup. The caller must validate disk identity, partition bounds,
 * ownership of the gap and absence of pending EFI/source updates, and hold an
 * exclusive lock until cleanup completes. Unknown journal contents fail shut. */
#pragma once
#include "upgrade_journal.h"
#define RSG_SHARED_PRIMARY 1996ull
#define RSG_SHARED_SHADOW 1997ull
static int rsg_shared_record(struct zu_journal *j, const uint8_t saved[512], uint8_t b[512]) {
    uint8_t gpt[512];
    if (!j->io.read(j->io.context,1,gpt))return 0;
    for (unsigned i=0;i<512;i++) b[i]=0;
    const char *m="RSGTXN01";for(unsigned i=0;i<8;i++)b[i]=(uint8_t)m[i];
    zu_put64(b+8,j->io.capacity);zu_put64(b+16,j->allowed_first);zu_put64(b+24,j->allowed_end);
    zu_put32(b+32,j->slots);for(unsigned i=0;i<16;i++)b[36+i]=saved[i];
    for(unsigned i=0;i<16;i++)b[52+i]=gpt[56+i];
    zu_put32(b+68,zeal_partition_crc(b,68));
    return 1;
}
static int rsg_shared_valid(struct zu_journal *j,const uint8_t b[512]) {
    uint8_t gpt[512];
    if(!j->io.read(j->io.context,1,gpt))return 0;
    const char *m="RSGTXN01";for(unsigned i=0;i<8;i++)if(b[i]!=(uint8_t)m[i])return 0;
    if(zeal_partition_u64(b+8)!=j->io.capacity || zeal_partition_u64(b+16)!=j->allowed_first ||
       zeal_partition_u64(b+24)!=j->allowed_end || zeal_partition_u32(b+32)!=j->slots ||
       zeal_partition_u32(b+68)!=zeal_partition_crc(b,68))return 0;
    for(unsigned i=0;i<16;i++)if(b[52+i]!=gpt[56+i])return 0;
    for(unsigned i=72;i<512;i++)if(b[i])return 0;
    return 1;
}
static int rsg_shared_write(struct zu_journal *j,uint64_t lba,const uint8_t b[512]) {
    uint8_t check[512];
    if(!j->io.write(j->io.context,lba,b)||!j->io.flush(j->io.context)||
       !j->io.read(j->io.context,lba,check))return 0;
    for(unsigned i=0;i<512;i++)if(check[i]!=b[i])return 0;
    return 1;
}
/* Only all-zero or a fully valid, zero-padded EFI commit can be retained.
 * A legacy 512-slot journal or pending source marker must be resolved by the
 * caller before invoking this helper. */
static int rsg_shared_saved_ok(struct zu_journal *j,const uint8_t b[512]) {
    if(zu_empty(b))return 1;
    const char *magic="ZUDONE01";
    for(unsigned i=0;i<8;i++)if(b[i]!=(uint8_t)magic[i])return 0;
    for(unsigned i=16;i<512;i++)if(b[i])return 0;
    uint8_t header[512]={0}; const char *hm="ZUJRNL02";
    for(unsigned i=0;i<8;i++)header[i]=(uint8_t)hm[i];
    zu_put32(header+8,j->slots);zu_put64(header+12,2048);zu_put64(header+20,130911);
    zu_put32(header+28,zeal_partition_crc(header,28));
    return zeal_partition_u32(b+8)==zeal_partition_crc(header,512) &&
           zeal_partition_u32(b+12)==zeal_partition_crc(b,12);
}

static int rsg_shared_begin(struct zu_journal *j) {
    uint8_t a[512],b[512],saved[512],marker[512];
    if(!j || !zu_valid_io(&j->io) || j->io.journal!=34 || j->slots!=650 ||
       j->io.capacity<=RSG_SHARED_SHADOW || j->allowed_first>=j->allowed_end ||
       j->allowed_end>j->io.capacity || j->allowed_first<2048 ||
       !j->io.read(j->io.context,RSG_SHARED_PRIMARY,a) || !zu_empty(a) ||
       !j->io.read(j->io.context,RSG_SHARED_SHADOW,b) || !zu_empty(b) ||
       !j->io.read(j->io.context,j->io.journal,a) || !zu_empty(a) ||
       !j->io.read(j->io.context,j->io.journal+2+3ull*j->slots,b) || !zu_empty(b) ||
       !j->io.read(j->io.context,j->io.journal+1+3ull*j->slots,saved) ||
       !rsg_shared_saved_ok(j,saved))return 0;
    if(!rsg_shared_record(j,saved,marker))return 0;
    if(!rsg_shared_write(j,RSG_SHARED_SHADOW,marker) ||
       !rsg_shared_write(j,RSG_SHARED_PRIMARY,marker))return 0;
    return zu_journal_begin(j);
}
static int rsg_shared_partial(const uint8_t *a,const uint8_t *expected) {
    /* A single torn write can leave either a prefix or a suffix of the
     * expected record, with zeroes elsewhere. */
    for(unsigned split=0;split<=512;split++) {
        int prefix=1,suffix=1;
        for(unsigned i=0;i<512;i++) {
            if(a[i]!=(i<split?expected[i]:0))prefix=0;
            if(a[i]!=(i<split?0:expected[i]))suffix=0;
        }
        if(prefix||suffix)return 1;
    }
    return 0;
}
/* Recover/retire the borrowed journal and restore the exact prior commit.
 * Even a failure during cleanup must be retried before EFI recovery starts. */
static int rsg_shared_recover(struct zu_journal *j) {
    uint8_t a[512],b[512],chosen[512],saved[512]={0},expected[512],zero[512]={0};
    if(!j || !zu_valid_io(&j->io) || j->io.journal!=34 || j->slots!=650 ||
       j->io.capacity<=RSG_SHARED_SHADOW || j->allowed_first<2048 ||
       j->allowed_first>=j->allowed_end || j->allowed_end>j->io.capacity ||
       !j->io.read(j->io.context,RSG_SHARED_PRIMARY,a) ||
       !j->io.read(j->io.context,RSG_SHARED_SHADOW,b))return 0;
    if(zu_empty(a)&&zu_empty(b))return 1;
    int av=rsg_shared_valid(j,a),bv=rsg_shared_valid(j,b);
    if(av&&bv)for(unsigned i=0;i<512;i++)if(a[i]!=b[i])return 0;
    if(av||bv) {
        for(unsigned i=0;i<512;i++)chosen[i]=av?a[i]:b[i];
        if((!av&&!rsg_shared_partial(a,chosen)) || (!bv&&!rsg_shared_partial(b,chosen)))return 0;
    } else {
        /* A torn first marker or last marker retirement: the original commit
         * is still current, and no transaction can have started. */
        if(!j->io.read(j->io.context,j->io.journal+1+3ull*j->slots,saved) ||
           !rsg_shared_saved_ok(j,saved))return 0;
        if(!rsg_shared_record(j,saved,expected))return 0;
        if(!rsg_shared_partial(a,expected)||!rsg_shared_partial(b,expected))return 0;
        for(unsigned i=0;i<512;i++)chosen[i]=expected[i];
    }
    for(unsigned i=0;i<16;i++)saved[i]=chosen[36+i];
    if(!rsg_shared_saved_ok(j,saved) || !zu_journal_recover(j))return 0;
    /* Clear stale descriptors and old images before relinquishing the gap. */
    for(uint64_t lba=j->io.journal+1;lba<j->io.journal+1+3ull*j->slots;lba++)
        if(!j->io.write(j->io.context,lba,zero))return 0;
    if(!j->io.flush(j->io.context) ||
       !rsg_shared_write(j,j->io.journal+1+3ull*j->slots,saved) ||
       !rsg_shared_write(j,RSG_SHARED_SHADOW,zero) ||
       !rsg_shared_write(j,RSG_SHARED_PRIMARY,zero))return 0;
    return 1;
}
