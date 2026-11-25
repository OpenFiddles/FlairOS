#ifndef FLAIROS_VFS_H
#define FLAIROS_VFS_H

#include <stdint.h>

#define VFS_MAX_NODES   64
#define VFS_MAX_FDS     16
#define VFS_NAME_MAX    32
#define VFS_PATH_MAX    128

#define VFS_TYPE_FILE   1u
#define VFS_TYPE_DIR    2u
#define VFS_TYPE_CHAR   3u

#define VFS_O_RDONLY    0u
#define VFS_O_WRONLY    1u
#define VFS_O_RDWR      2u
#define VFS_O_CREAT     0x40u
#define VFS_O_TRUNC     0x200u
#define VFS_O_APPEND    0x400u

#define VFS_SEEK_SET    0
#define VFS_SEEK_CUR    1
#define VFS_SEEK_END    2

struct vfs_stat {
    uint32_t size;
    uint32_t type;
    uint32_t mtime;
};

struct vfs_dirent {
    char name[VFS_NAME_MAX];
    uint32_t type;
    uint32_t size;
};

int vfs_init(void);
int vfs_mkdir(const char *path);
int vfs_unlink(const char *path);
int vfs_stat(const char *path, struct vfs_stat *out);
int vfs_open(const char *path, uint32_t flags);
int vfs_close(int fd);
int vfs_read(int fd, void *buf, uint32_t count);
int vfs_write(int fd, const void *buf, uint32_t count);
int vfs_seek(int fd, int32_t offset, int whence);
int vfs_readdir(const char *path, uint32_t index, struct vfs_dirent *out);
int vfs_chdir(const char *path);
int vfs_getcwd(char *buf, uint32_t size);
int vfs_dup2(int oldfd, int newfd);

int vfs_has_persistent_storage(void);
#endif
