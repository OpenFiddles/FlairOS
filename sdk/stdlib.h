#ifndef FLARE_C_STDLIB_H
#define FLARE_C_STDLIB_H
#include "stddef.h"

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

int abs(int n);
int atoi(const char *s);
void *malloc(size_t size);
void free(void *ptr);
void *calloc(size_t count, size_t size);
void *realloc(void *ptr, size_t size);

#endif
