#define _XOPEN_SOURCE 700
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../src/redsea_growth_utm_coordination.h"
struct disk { int fd; uint64_t inject; unsigned writes; };
static int rd(void *ctx,uint64_t lba,uint8_t out[512]) {
    struct disk *d=ctx;
    if(pread(d->fd,out,512,(off_t)(lba*512))!=512)return 0;
    if(lba==d->inject) {
        if(lba==1572) {
            memset(out,0,512);memcpy(out,"ZUJRNL02",8);
            zu_put32(out+8,512);zu_put64(out+12,2048);zu_put64(out+20,130911);
            zu_put32(out+28,zeal_partition_crc(out,28));
        } else out[0]^=0xff;
    }
    return 1;
}
static int wr(void *ctx,uint64_t lba,const uint8_t in[512]) {
    struct disk *d=ctx;(void)lba;(void)in;d->writes++;return 0;
}
static int flush(void *ctx) {(void)ctx;return 0;}
int main(int argc,char **argv) {
    if(argc!=2)return 2;
    struct disk d={.fd=open(argv[1],O_RDONLY),.inject=UINT64_MAX}; struct stat st;
    if(d.fd<0 || fstat(d.fd,&st) || st.st_size%512)return 3;
    struct zu_commit_io io={&d,rd,wr,flush,(uint64_t)st.st_size/512,34};
    struct zu_journal j;
    if(!rsg_utm_admit(io,&j) || d.writes || j.allowed_first!=130911 ||
       j.allowed_end<=j.allowed_first) {fprintf(stderr,"valid disk not admitted read-only\n");return 4;}
    const uint64_t conflicts[]={34,1572,1988,1996};
    for(unsigned i=0;i<sizeof(conflicts)/sizeof(conflicts[0]);i++) {
        d.inject=conflicts[i];
        if(rsg_utm_admit(io,&j) || d.writes) {fprintf(stderr,"conflicting state was admitted\n");return 5;}
    }
    close(d.fd);puts("PASS UTM journal admission is read-only and rejects active/pending/foreign state");return 0;
}
