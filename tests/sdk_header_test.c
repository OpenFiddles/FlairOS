#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>
#include <errno.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include "flare_libc.h"

static int compare_ints(const void *a, const void *b) {
    int x = *(const int *)a;
    int y = *(const int *)b;
    return (x > y) - (x < y);
}

int sdk_header_probe(void) {
    char text[64];
    int values[3] = { 3, 1, 2 };
    bool ok = true;
    qsort(values, 3, sizeof values[0], compare_ints);
    (void)snprintf(text, sizeof text, "%s:%d", "sdk", values[0]);
    if (!isalpha((unsigned char)text[0]) || strcmp(text, "sdk:1") != 0)
        ok = false;
    if (strtol("42", 0, 10) != 42 || errno != 0)
        ok = false;
    assert(ok);
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
