#ifndef FLARE_C_DIRENT_H
#define FLARE_C_DIRENT_H
#define DT_UNKNOWN 0
#define DT_DIR 4
#define DT_REG 8
struct dirent { unsigned char d_type; char d_name[256]; };
typedef struct { int fd; int idx; } DIR;
DIR *opendir(const char *path);
struct dirent *readdir(DIR *d);
int closedir(DIR *d);
#endif
