#include <stdio.h>
#include <string.h>
#define FLAIROS_HOST_TEST 1
#include "../kernel/vfs.c"

#define CHECK(x) do { if (!(x)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (0)

int main(void) {
    CHECK(vfs_init() == 0);

    char cwd[64];
    CHECK(vfs_getcwd(cwd, sizeof(cwd)) == 0);
    CHECK(strcmp(cwd, "/") == 0);

    CHECK(vfs_mkdir("/lib/python") == 0);
    CHECK(vfs_chdir("/lib/python") == 0);
    CHECK(vfs_getcwd(cwd, sizeof(cwd)) == 0);
    CHECK(strcmp(cwd, "/lib/python") == 0);

    int fd = vfs_open("sys.py", VFS_O_CREAT | VFS_O_RDWR);
    CHECK(fd >= 3);
    const char *msg = "print('hi')\n";
    CHECK(vfs_write(fd, msg, (uint32_t)strlen(msg)) == (int)strlen(msg));
    CHECK(vfs_seek(fd, 0, VFS_SEEK_SET) == 0);

    char buf[32];
    int n = vfs_read(fd, buf, sizeof(buf) - 1);
    CHECK(n == (int)strlen(msg));
    buf[n] = 0;
    CHECK(strcmp(buf, msg) == 0);
    CHECK(vfs_close(fd) == 0);

    struct vfs_stat st;
    CHECK(vfs_stat("/lib/python/sys.py", &st) == 0);
    CHECK(st.type == VFS_TYPE_FILE && st.size == strlen(msg));

    struct vfs_dirent de;
    CHECK(vfs_readdir("/lib/python", 0, &de) == 0);
    CHECK(strcmp(de.name, "sys.py") == 0);
    CHECK(vfs_readdir("/lib/python", 1, &de) != 0);

    CHECK(vfs_unlink("/lib/python/sys.py") == 0);
    CHECK(vfs_stat("/lib/python/sys.py", &st) != 0);

    /* Node and descriptor tables grow instead of stopping at their old fixed limits. */
    for (int i = 0; i < 90; ++i) {
        char path[32];
        snprintf(path, sizeof(path), "/home/d%02d", i);
        CHECK(vfs_mkdir(path) == 0);
    }
    int many[24];
    for (int i = 0; i < 24; ++i) {
        char path[32];
        snprintf(path, sizeof(path), "/home/f%02d", i);
        many[i] = vfs_open(path, VFS_O_CREAT | VFS_O_RDWR);
        CHECK(many[i] >= 3);
    }
    for (int i = 0; i < 24; ++i) CHECK(vfs_close(many[i]) == 0);

    puts("VFS hierarchical filesystem host test: all checks passed");
    return 0;
}
