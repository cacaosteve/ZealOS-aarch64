#define main rsg_previous_test_main
#include "redsea_growth_transaction_test.c"
#undef main
#include "../src/redsea_growth_shared_journal.h"
#include "../src/efi_upgrade_owner.h"
static void prior_commit(struct disk *d) {
    uint8_t h[512]={0},*c=d->s[1985];
    memcpy(h,"ZUJRNL02",8);zu_put32(h+8,650);zu_put64(h+12,2048);zu_put64(h+20,130911);
    zu_put32(h+28,zeal_partition_crc(h,28));memcpy(c,"ZUDONE01",8);
    zu_put32(c+8,zeal_partition_crc(h,512));zu_put32(c+12,zeal_partition_crc(c,12));
    for(unsigned i=0;i<16;i++)d->s[1][56+i]=(uint8_t)(i+1);
}
static int check_recovered(struct disk *d,struct zu_journal *j,const uint8_t prior[512]) {
    if(!rsg_shared_recover(j) || memcmp(d->s[1985],prior,512) ||
       !zu_empty(d->s[1996]) || !zu_empty(d->s[1997]) || memcmp(d->s[2061],"hello",5))return 0;
    uint64_t root=rsg_u64(d->s[2048]+24), parent=rsg_u64(d->s[2060]+104);
    return (root==2050 && parent==2050)||(root==2070 && parent==2070);
}
int main(void) {
    struct disk *d=malloc(sizeof(*d));struct rsg_ownership *o=malloc(sizeof(*o));
    if(!d||!o)return 1;init(d);prior_commit(d);
    uint8_t prior[512];memcpy(prior,d->s[1985],512);
    struct zu_journal j=journal(d);j.io.capacity=140000;
    struct zu_journal owner_efi=j;owner_efi.allowed_first=2048;owner_efi.allowed_end=130911;
    if(!zu_owner_prepare(&owner_efi,1))return 21;
    if(!rsg_shared_begin(&j)||!rsg_grow(&j,2048,152,1,0,2050,2070,2048,0,o)||
       !zu_journal_commit(&j)||!check_recovered(d,&j,prior))return 2;
    unsigned operations=d->calls;
    struct zu_journal efi=j;efi.allowed_first=2048;efi.allowed_end=130911;
    if(zu_journal_committed(&efi)!=1 || !zu_owner_prepare(&efi,1))return 3;
    /* Interrupt every read, write or flush; failed writes may tear. */
    for(unsigned t=0;t<4;t++)for(unsigned cut=1;cut<=operations;cut++) {
        init(d);prior_commit(d);j=journal(d);j.io.capacity=140000;
        d->calls=0;d->fail=cut;d->tear=t==0?0:t==1?7:t==2?256:511;
        if(rsg_shared_begin(&j)) {
            if(rsg_grow(&j,2048,152,1,0,2050,2070,2048,0,o)) (void)zu_journal_commit(&j);
        }
        (void)rsg_shared_recover(&j);
        d->fail=0;d->tear=0;
        if(!check_recovered(d,&j,prior)) {fprintf(stderr,"failed cut %u tear %d\n",cut,t);return 4;}
    }
    /* A claim copied to a different disk must not authorize recovery. */
    init(d);prior_commit(d);j=journal(d);j.io.capacity=140000;
    if(!rsg_shared_begin(&j))return 23;
    d->s[1][56]^=0x80;
    if(rsg_shared_recover(&j))return 24;
    d->s[1][56]^=0x80;
    if(!rsg_shared_recover(&j))return 25;
    /* Fresh installs have no prior EFI commit record. */
    init(d);j=journal(d);j.io.capacity=140000;
    if(!rsg_shared_begin(&j)||!rsg_grow(&j,2048,152,1,0,2050,2070,2048,0,o)||
       !zu_journal_commit(&j)||!rsg_shared_recover(&j)||!zu_empty(d->s[1985]))return 26;
    for(unsigned which=0;which<3;which++) {
        init(d);prior_commit(d);j=journal(d);j.io.capacity=140000;
        uint64_t bad=which==0?34:which==1?1996:1985;
        d->s[bad][0]^=0xff;
        uint8_t before[512];memcpy(before,d->s[bad],512);
        if(rsg_shared_begin(&j) || memcmp(before,d->s[bad],512))return 22;
    }
    free(o);free(d);printf("PASS prototype journal borrowing preserves EFI commit across %u interruption points and torn writes\n",operations);return 0;
}
