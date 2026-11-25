#ifndef FLARE_C_UNISTD_H
#define FLARE_C_UNISTD_H
#include "stddef.h"

int read(int fd, void *buf, unsigned int count);
int write(int fd, const void *buf, unsigned int count);
int close(int fd);
int lseek(int fd, int offset, int whence);
int unlink(const char *path);
int chdir(const char *path);
char *getcwd(char *buf, unsigned int size);
int isatty(int fd);
int getpid(void);
unsigned int sleep(unsigned int seconds);
int usleep(unsigned int usec);

#endif
