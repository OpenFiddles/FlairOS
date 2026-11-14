#ifndef FLAIR_PCK_H
#define FLAIR_PCK_H

/* Pancake data/cache container. PCK files contain resources, never executable code.
 * All integers are little-endian. Offsets are absolute from the start of the file.
 */
#define PCK_MAGIC "PCK1"
#define PCK_VERSION 1u
#define PCK_HEADER_SIZE 16u
#define PCK_NAME_MAX 31u
#define PCK_ENTRY_SIZE 52u

struct pck_header {
    char magic[4];
    unsigned short version;
    unsigned short entry_count;
    unsigned int index_size;
    unsigned int data_offset;
} __attribute__((packed));

struct pck_entry {
    char name[PCK_NAME_MAX + 1];
    unsigned long long offset;
    unsigned long long size;
    unsigned int crc32;
} __attribute__((packed));

#endif
