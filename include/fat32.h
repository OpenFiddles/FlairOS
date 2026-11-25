#ifndef FLAIROS_FAT32_H
#define FLAIROS_FAT32_H

#include <stdint.h>

#define FAT32_ATTR_READONLY 0x01u
#define FAT32_ATTR_DIR      0x10u
#define FAT32_EOC      0x0FFFFFF8u

typedef int (*fat32_read_fn)(void *ctx, uint32_t lba, uint8_t *sec512);
typedef int (*fat32_write_fn)(void *ctx, uint32_t lba, const uint8_t *sec512);

struct fat32_fs {
    fat32_read_fn read_sector;
    fat32_write_fn write_sector;
    void *ctx;
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  num_fats;
    uint32_t fat_size_sectors;
    uint32_t root_cluster;
    uint32_t fat_lba;
    uint32_t data_lba;
    uint32_t total_sectors;
    uint32_t cluster_count;
};

struct fat32_dirent {
    char name[13];
    uint8_t attr;
    uint32_t cluster;
    uint32_t size;
};

int fat32_mount(struct fat32_fs *fs, fat32_read_fn read_fn, void *ctx, uint32_t part_lba);
int fat32_set_writer(struct fat32_fs *fs, fat32_write_fn write_fn);
int fat32_create_file(struct fat32_fs *fs, uint32_t dir_cluster, const char *name83, struct fat32_dirent *out);
int fat32_write_file(struct fat32_fs *fs, uint32_t dir_cluster, const char *name83, const void *data, uint32_t size);
int fat32_mkdir(struct fat32_fs *fs, uint32_t dir_cluster, const char *name83, struct fat32_dirent *out);
int fat32_delete_file(struct fat32_fs *fs, uint32_t dir_cluster, const char *name83);
uint32_t fat32_next_cluster(const struct fat32_fs *fs, uint32_t cluster);
int fat32_find_in_dir(const struct fat32_fs *fs, uint32_t dir_cluster,
                      const char *name83, struct fat32_dirent *out);
int fat32_read_file(const struct fat32_fs *fs, const struct fat32_dirent *ent,
                    void *buf, uint32_t max_bytes);
int fat32_readdir(const struct fat32_fs *fs, uint32_t dir_cluster,
                  uint32_t index, struct fat32_dirent *out);

#endif
