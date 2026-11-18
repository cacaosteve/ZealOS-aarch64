#define _XOPEN_SOURCE 700
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../src/redsea_growth_utm_coordination.h"
#include "../src/redsea_growth_transaction.h"
struct disk {int fd; unsigned calls,fail; int tear,triggered;};
static int rd(void *p,uint64_t l,uint8_t b[512]) {struct disk *d=p;if(++d->calls==d->fail){d->triggered=1;return 0;}return pread(d->fd,b,512,(off_t)(l*512))==512;}
static int wr(void *p,uint64_t l,const uint8_t b[512]) {struct disk *d=p;if(++d->calls==d->fail){d->triggered=1;if(d->tear)(void)pwrite(d->fd,b,d->tear,(off_t)(l*512));return 0;}return pwrite(d->fd,b,512,(off_t)(l*512))==512;}
static int fl(void *p) {struct disk *d=p;if(++d->calls==d->fail){d->triggered=1;return 0;}return fsync(d->fd)==0;}
static int find(struct disk *d,uint64_t physical,const char *name,uint64_t *first,uint64_t *size) {
 uint8_t b[512];if(!rd(d,physical,b))return 0;uint64_t bytes=zeal_partition_u64(b+48);
 if(!bytes || (bytes&511) || bytes>65536)return 0;
 for(uint64_t n=0;n<bytes/512;n++){if(n&&!rd(d,physical+n,b))return 0;for(unsigned k=0;k<8;k++){
  uint8_t *e=b+64*k;if(!e[2])return 0;if(e[1]&1)continue;
  if(!strcmp((const char*)e+2,name)){*first=zeal_partition_u64(e+40);*size=zeal_partition_u64(e+48);return 1;}
 }}return 0;
}
static int read_hello(struct disk *d,struct zu_journal *j,uint8_t out[512],uint64_t *length) {
 uint8_t b[512];uint64_t root,home,n,first;if(!rd(d,j->allowed_first,b))return 0;
 uint64_t bias=j->allowed_first-zeal_partition_u64(b+8);root=zeal_partition_u64(b+24)+bias;
 return find(d,root,"Home",&home,&n) && find(d,home+bias,"Hello.TXT",&first,length) &&
        *length>0 && *length<=512 && rd(d,first+bias,out);
}
int main(int argc,char **argv) {
 if(argc!=4 && argc!=5)return 1;struct stat lst,st;
 if(lstat(argv[1],&lst)||!S_ISREG(lst.st_mode)||lst.st_nlink!=1)return 2;
 struct disk d={.fd=open(argv[1],O_RDWR),.fail=0};
 if(d.fd<0||fstat(d.fd,&st)||!S_ISREG(st.st_mode)||st.st_nlink!=1||
    lst.st_dev!=st.st_dev||lst.st_ino!=st.st_ino||st.st_size%512)return 2;
 struct zu_commit_io io={&d,rd,wr,fl,(uint64_t)st.st_size/512,34};struct zu_journal j;
 if(!rsg_utm_admit(io,&j))return 3;
 struct rsg_ownership *o=malloc(sizeof(*o));if(!o)return 4;
 uint8_t boot[512],before[512],after[512],commit[512],commit_after[512];uint64_t len,len2;
 if(!rd(&d,j.allowed_first,boot)||!read_hello(&d,&j,before,&len)||!rd(&d,1985,commit))return 5;
 uint64_t base=j.allowed_first,bias=base-zeal_partition_u64(boot+8),sectors=zeal_partition_u64(boot+16);
 uint64_t maps=zeal_partition_u64(boot+32),old=zeal_partition_u64(boot+24)+bias,blocks;
 uint8_t root[512];if(!rd(&d,old,root))return 6;blocks=zeal_partition_u64(root+48)/512;
 if(!blocks||!rsg_ownership_check(&j,base,sectors,maps,bias,o))return 7;
 uint64_t fresh=0;int bit;for(uint64_t p=base+maps+1;p+blocks*2<=base+sectors;p++){
  uint64_t n=0;for(;n<blocks*2;n++)if(!rsg_bit(&j,base,sectors,maps,p+n,&bit)||bit)break;
  if(n==blocks*2){fresh=p;break;}p+=n;
 }
 if(!fresh)return 8;d.calls=0;d.fail=(unsigned)strtoul(argv[2],0,10);d.tear=atoi(argv[3]);
 int started=rsg_shared_begin(&j),grown=0,committed=0;
 if(started)grown=rsg_grow(&j,base,sectors,maps,bias,old,fresh,base,0,o);
 if(grown)committed=zu_journal_commit(&j);
 if(argc==5 && !strcmp(argv[4],"pending")) {
  if(!d.triggered) return 14;
  printf("PENDING started=%d grown=%d committed=%d calls=%u\n",started,grown,committed,d.calls);
  free(o);if(close(d.fd))return 10;return 0;
 }
 /* Reopen before recovery, and permit the injected interruption to fall
  * during recovery as well. Then reopen a second time and retry recovery. */
 if(close(d.fd))return 10;
 d.fd=open(argv[1],O_RDWR);if(d.fd<0)return 11;
 (void)rsg_shared_recover(&j);
 unsigned operation_calls=d.calls;int triggered=d.triggered;d.fail=0;d.tear=0;
 if(strtoul(argv[2],0,10) && !triggered)return 14;
 if(close(d.fd))return 12;
 d.fd=open(argv[1],O_RDWR);if(d.fd<0)return 13;
 if(!rsg_shared_recover(&j)||!rsg_utm_admit(io,&j)||
    !rsg_ownership_check(&j,base,sectors,maps,bias,o)||
    !read_hello(&d,&j,after,&len2)||len!=len2||memcmp(before,after,len)||
    !rd(&d,1985,commit_after)||memcmp(commit,commit_after,512))return 9;
 printf("PASS file-backed disposable image: failure=%s tear=%s started=%d grown=%d committed=%d operations=%u; Hello.TXT and EFI commit intact\n",argv[2],argv[3],started,grown,committed,operation_calls);
 free(o);close(d.fd);return 0;
}
