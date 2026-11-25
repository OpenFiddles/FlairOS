#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fat32.h"

#define SECTORS 70000u
#define CHECK(x) do { if (!(x)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); free(disk); return 1; } } while (0)

static uint8_t *disk;
static int mem_read(void *ctx, uint32_t lba, uint8_t *sec) {
    (void)ctx;
    if (!disk || lba >= SECTORS) return -1;
    memcpy(sec, disk + (size_t)lba * 512u, 512);
    return 0;
}
static int mem_write(void *ctx, uint32_t lba, const uint8_t *sec) {
    (void)ctx;
    if (!disk || lba >= SECTORS) return -1;
    memcpy(disk + (size_t)lba * 512u, sec, 512);
    return 0;
}
static void wr32(uint8_t *p, uint32_t v) {
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static void fat_entry(uint32_t fat_lba, uint32_t cl, uint32_t val) {
    uint8_t *p=disk+(size_t)fat_lba*512u+(size_t)cl*4u; wr32(p,val);
}

int main(void) {
    disk=(uint8_t *)calloc(SECTORS,512u);
    CHECK(disk != 0);
    /* 70,000-sector FAT32 volume: 1 reserved sector, two 547-sector FATs. */
    uint8_t *b=disk;
    b[11]=0; b[12]=2; b[13]=1; b[14]=1; b[16]=2;
    wr32(b+36,547); wr32(b+32,SECTORS); wr32(b+44,2);
    b[510]=0x55; b[511]=0xAA;
    const uint32_t fat1=1, fat2=548, data=1095;
    for (int copy=0;copy<2;++copy) {
        uint32_t f=copy?fat2:fat1;
        fat_entry(f,0,0x0FFFFFF8u); fat_entry(f,1,0x0FFFFFFFu);
        fat_entry(f,2,0x0FFFFFFFu); fat_entry(f,3,0x0FFFFFFFu);
    }
    /* Root directory: HELLO.TXT -> cluster 3, 12 bytes. */
    uint8_t *root=disk+(size_t)data*512u;
    memcpy(root,"HELLO   TXT",11); root[11]=0x20; root[26]=3; root[28]=12;
    memcpy(disk+(size_t)(data+1)*512u,"FlairOSFAT32",12);

    struct fat32_fs fs;
    CHECK(fat32_mount(&fs,mem_read,0,0)==0);
    CHECK(fat32_write_file(&fs,fs.root_cluster,"HELLO.TXT","blocked",7)==-1);
    CHECK(fat32_create_file(&fs,fs.root_cluster,"BLOCKED.TXT",0)==-1);
    CHECK(fat32_set_writer(&fs,mem_write)==0);

    struct fat32_dirent ent;
    CHECK(fat32_find_in_dir(&fs,fs.root_cluster,"hello.txt",&ent)==0);
    CHECK(ent.size==12 && ent.cluster==3);
    char buf[1024]; memset(buf,0,sizeof(buf));
    CHECK(fat32_read_file(&fs,&ent,buf,sizeof(buf))==12);
    CHECK(strcmp(buf,"FlairOSFAT32")==0);

    /* Replace contents across two clusters and verify mirrored FAT metadata. */
    char payload[700]; for (int i=0;i<700;++i) payload[i]=(char)('A'+(i%26));
    CHECK(fat32_write_file(&fs,fs.root_cluster,"HELLO.TXT",payload,sizeof(payload))==0);
    CHECK(fat32_find_in_dir(&fs,fs.root_cluster,"HELLO.TXT",&ent)==0);
    CHECK(ent.size==700 && ent.cluster>=4);
    memset(buf,0,sizeof(buf));
    CHECK(fat32_read_file(&fs,&ent,buf,sizeof(buf))==700);
    CHECK(memcmp(buf,payload,700)==0);
    uint32_t first=ent.cluster;
    uint32_t second=rd32(disk+(size_t)fat1*512u+(size_t)first*4u)&0x0FFFFFFFu;
    CHECK(second>=2 && second<0x0FFFFFF8u);
    CHECK((rd32(disk+(size_t)fat2*512u+(size_t)first*4u)&0x0FFFFFFFu)==second);

    CHECK(fat32_create_file(&fs,fs.root_cluster,"NEWFILE.BIN",&ent)==0);
    CHECK(fat32_write_file(&fs,fs.root_cluster,"NEWFILE.BIN","persistent",10)==0);
    CHECK(fat32_find_in_dir(&fs,fs.root_cluster,"NEWFILE.BIN",&ent)==0);
    CHECK(ent.size==10);
    memset(buf,0,sizeof(buf)); CHECK(fat32_read_file(&fs,&ent,buf,sizeof(buf))==10);
    CHECK(memcmp(buf,"persistent",10)==0);
    CHECK(fat32_create_file(&fs,fs.root_cluster,"newfile.bin",0)==-1);
    CHECK(fat32_create_file(&fs,fs.root_cluster,"TOO-LONG-NAME.TXT",0)==-1);
    CHECK(fat32_delete_file(&fs,fs.root_cluster,"NEWFILE.BIN")==0);
    CHECK(fat32_find_in_dir(&fs,fs.root_cluster,"NEWFILE.BIN",&ent)==-1);
    CHECK(fat32_delete_file(&fs,fs.root_cluster,"HELLO.TXT")==0);
    CHECK(fat32_find_in_dir(&fs,fs.root_cluster,"HELLO.TXT",&ent)==-1);
    CHECK((rd32(disk+(size_t)fat1*512u+(size_t)first*4u)&0x0FFFFFFFu)==0);
    CHECK((rd32(disk+(size_t)fat2*512u+(size_t)first*4u)&0x0FFFFFFFu)==0);

    puts("FAT32 host test: mount/read/create/write/delete and mirrored FAT checks passed");
    free(disk);
    return 0;
}
