#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/redsea_growth_transaction.h"
#define CAP 2200
struct disk { unsigned calls, fail; int tear; unsigned char s[CAP][512]; };
static int rd(void *p,uint64_t l,uint8_t b[512]) { struct disk *d=p; if(l>=CAP || ++d->calls==d->fail)return 0; memcpy(b,d->s[l],512); return 1; }
static int wr(void *p,uint64_t l,const uint8_t b[512]) { struct disk *d=p; if(l>=CAP)return 0; if(++d->calls==d->fail) { if(d->tear)memcpy(d->s[l],b,d->tear); return 0; } memcpy(d->s[l],b,512); return 1; }
static int flush(void *p) { struct disk *d=p; return ++d->calls!=d->fail; }
static void init(struct disk *d) {
    memset(d,0,sizeof(*d));
    uint8_t *b=d->s[2048]; b[3]=0x88; b[510]=0x55;b[511]=0xaa;
    zu_put64(b+8,2048);zu_put64(b+16,152);zu_put64(b+24,2050);zu_put64(b+32,1);
    /* Reserved bit zero, root bit one, child bit eleven, and file bit 12. */
    d->s[2049][0]=3;d->s[2049][1]=0x18;
    b=d->s[2050]; b[0]=0x10;b[1]=4;b[2]='.';zu_put64(b+40,2050);zu_put64(b+48,512);
    b[64]=0x10;b[65]=4;b[66]='.';b[67]='.';zu_put64(b+104,2050);
    b[128]=0x10;b[129]=4;memcpy(b+130,"CHILD",5);zu_put64(b+168,2060);zu_put64(b+176,512);
    b[192]=0;b[193]=4;memcpy(b+194,"KEEP.TXT",8);zu_put64(b+232,2061);zu_put64(b+240,5);
    memcpy(d->s[2061],"hello",5);
    b=d->s[2060];b[0]=0x10;b[1]=4;b[2]='.';zu_put64(b+40,2060);zu_put64(b+48,512);
    b[64]=0x10;b[65]=4;b[66]='.';b[67]='.';zu_put64(b+104,2050);
}
static struct zu_journal journal(struct disk *d) {
    struct zu_journal j={{d,rd,wr,flush,CAP,34},2048,2200,650,0}; return j;
}
int main(void) {
    struct disk *d=malloc(sizeof(*d));
    struct rsg_ownership *owner=malloc(sizeof(*owner));
    if(!d || !owner)return 1;
    init(d);struct zu_journal j=journal(d);
    if(!zu_journal_begin(&j))return 2;
    d->calls=0;
    if(!rsg_grow(&j,2048,152,1,0,2050,2070,2048,0,owner))return 2;
    unsigned growth_calls=d->calls;
    if(!zu_journal_commit(&j))return 2;
    if(rsg_u64(d->s[2048]+24)!=2070 || rsg_u64(d->s[2070]+40)!=2070 ||
       rsg_u64(d->s[2070]+48)!=1024 || rsg_u64(d->s[2070]+104)!=2070 ||
       !rsg_ownership_check(&j,2048,152,1,0,owner) || rsg_u64(d->s[2060]+104)!=2070 ||
       memcmp(d->s[2061],"hello",5) || (d->s[2049][0]&2))return 3;
    /* Grow a non-root directory containing a nested child. */
    init(d); d->s[2049][1] |= 0x20; /* bit 13: sector 2062 */
    uint8_t *sub=d->s[2060]; sub[128]=0x10; sub[129]=4;
    memcpy(sub+130,"NEST",4); zu_put64(sub+168,2062); zu_put64(sub+176,512);
    uint8_t *nested=d->s[2062]; nested[0]=0x10; nested[1]=4; nested[2]='.';
    zu_put64(nested+40,2062);zu_put64(nested+48,512);nested[64]=0x10;nested[65]=4;
    nested[66]='.';nested[67]='.';zu_put64(nested+104,2060);
    struct disk *nonroot=malloc(sizeof(*nonroot));if(!nonroot)return 9;
    memcpy(nonroot,d,sizeof(*d));j=journal(d);
    if(!zu_journal_begin(&j))return 9;
    d->calls=0;
    if(!rsg_grow(&j,2048,152,1,0,2060,2070,2050,128,owner))return 9;
    unsigned nested_calls=d->calls;
    if(!zu_journal_commit(&j)||rsg_u64(d->s[2050]+168)!=2070||
       rsg_u64(d->s[2050]+176)!=1024||rsg_u64(d->s[2062]+104)!=2070||
       memcmp(d->s[2061],"hello",5)) return 9;
    for(int tear=0;tear<=256;tear+=128) for(unsigned cut=1;cut<=nested_calls;cut++) {
        memcpy(d,nonroot,sizeof(*d));j=journal(d);if(!zu_journal_begin(&j))return 10;
        d->calls=0;d->fail=cut;d->tear=tear;
        int ok=rsg_grow(&j,2048,152,1,0,2060,2070,2050,128,owner);
        d->fail=0;d->tear=0;
        if(ok) {if(!zu_journal_commit(&j))return 11;}
        else if(!zu_journal_recover(&j))return 12;
        if(memcmp(d->s[2061],"hello",5))return 13;
        if(!ok && (rsg_u64(d->s[2050]+168)!=2060 ||
                   rsg_u64(d->s[2050]+176)!=512 ||
                   rsg_u64(d->s[2062]+104)!=2060 || d->s[2049][1]!=0x38))return 14;
    }
    free(nonroot);
    for(int tear=0;tear<=256;tear+=128) for(unsigned cut=1;cut<=growth_calls;cut++) {
        init(d);j=journal(d);if(!zu_journal_begin(&j))return 4;
        d->calls=0;d->fail=cut;d->tear=tear;
        int ok=rsg_grow(&j,2048,152,1,0,2050,2070,2048,0,owner);
        d->fail=0;d->tear=0;
        if(ok) { if(!zu_journal_commit(&j))return 5; }
        else if(!zu_journal_recover(&j)) { fprintf(stderr,"recovery failure at %u tear %d\n",cut,tear);return 6; }
        if(memcmp(d->s[2061],"hello",5))return 7;
        if(!ok && (rsg_u64(d->s[2048]+24)!=2050 || rsg_u64(d->s[2050]+40)!=2050 ||
                   rsg_u64(d->s[2060]+104)!=2050 || d->s[2049][0]!=3 || d->s[2049][1]!=0x18)) {
            fprintf(stderr,"metadata changed at %u tear %d\n",cut,tear);return 8;
        }
    }
    /* Expanded UTM/Pi partition layouts store addresses relative to the
     * partition; journal targets remain physical disk sectors. */
    init(d);zu_put64(d->s[2048]+8,0);zu_put64(d->s[2048]+24,2);
    zu_put64(d->s[2050]+40,2);zu_put64(d->s[2050]+104,2);
    zu_put64(d->s[2050]+168,12);zu_put64(d->s[2050]+232,13);
    zu_put64(d->s[2060]+40,12);zu_put64(d->s[2060]+104,2);
    struct disk *relative=malloc(sizeof(*relative));if(!relative)return 27;
    memcpy(relative,d,sizeof(*d));j=journal(d);if(!zu_journal_begin(&j))return 28;
    d->calls=0;if(!rsg_grow(&j,2048,152,1,2048,2050,2070,2048,0,owner))return 29;
    unsigned relative_calls=d->calls;
    if(!zu_journal_commit(&j)||rsg_u64(d->s[2048]+24)!=22||
       rsg_u64(d->s[2070]+40)!=22||rsg_u64(d->s[2070]+104)!=22||
       !rsg_ownership_check(&j,2048,152,1,2048,owner)||rsg_u64(d->s[2060]+104)!=22||
       memcmp(d->s[2061],"hello",5))return 30;
    for(int tear=0;tear<=256;tear+=128) for(unsigned cut=1;cut<=relative_calls;cut++) {
        memcpy(d,relative,sizeof(*d));j=journal(d);if(!zu_journal_begin(&j))return 31;
        d->calls=0;d->fail=cut;d->tear=tear;
        int ok=rsg_grow(&j,2048,152,1,2048,2050,2070,2048,0,owner);
        d->fail=0;d->tear=0;
        if(ok) {if(!zu_journal_commit(&j))return 32;}
        else if(!zu_journal_recover(&j))return 33;
        if(memcmp(d->s[2061],"hello",5))return 34;
        if(!ok && (rsg_u64(d->s[2048]+24)!=2 || rsg_u64(d->s[2050]+40)!=2 ||
                   rsg_u64(d->s[2060]+104)!=2))return 35;
    }
    free(relative);
    /* Reject cross-linked files, a directory cycle, and a missing bitmap bit
     * before issuing any journalled metadata update. */
    for(unsigned corruption=0;corruption<3;corruption++) {
        init(d);
        if(corruption==0) {
            uint8_t *e=d->s[2050]+256; e[1]=4;memcpy(e+2,"ALIAS.TXT",9);
            zu_put64(e+40,2061);zu_put64(e+48,5);
        } else if(corruption==1) {
            zu_put64(d->s[2050]+168,2050);
        } else {
            d->s[2049][1] &= (uint8_t)~0x10;
        }
        j=journal(d);if(!zu_journal_begin(&j))return 18;
        if(rsg_grow(&j,2048,152,1,0,2050,2070,2048,0,owner)||j.next!=0)return 19;
        if(!zu_journal_recover(&j) || memcmp(d->s[2061],"hello",5))return 20;
    }
    for(int tear=0;tear<=256;tear+=128) for(unsigned cut=1;cut<=25;cut++) {
        init(d);j=journal(d);
        if(!zu_journal_begin(&j)||!rsg_grow(&j,2048,152,1,0,2050,2070,2048,0,owner))return 15;
        d->calls=0;d->fail=cut;d->tear=tear;(void)zu_journal_commit(&j);
        d->fail=0;d->tear=0;
        if(!zu_journal_recover(&j))return 16;
        uint64_t root=rsg_u64(d->s[2048]+24), parent=rsg_u64(d->s[2060]+104);
        if(!((root==2050 && parent==2050) || (root==2070 && parent==2070)) ||
           memcmp(d->s[2061],"hello",5))return 17;
    }
    free(owner);free(d);printf("PASS journalled root (%u), nested (%u), and partition-relative (%u) relocation failure points, torn writes, interrupted commits, and rejected cross-links/cycles\n",growth_calls,nested_calls,relative_calls);return 0;
}
