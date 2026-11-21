/*
FILE PATH: tests/sdk_host_test.c
PURPOSE: Host checks for sdk/flare_string.h: memory, string, number and heap helpers.
Runs on this machine without the kernel. Build and run with `make sdk-test`.
*/

#include <stdio.h>
#include "flare_string.h"

static int failures = 0;

#define CHECK(cond)                                  \
    do {                                             \
        if (cond) {                                  \
            printf("PASS %s\n", #cond);              \
        } else {                                     \
            printf("FAIL %s (line %d)\n", #cond, __LINE__); \
            failures++;                              \
        }                                            \
    } while (0)

static void test_memory(void) {
    char a[8] = "abcdef";
    char b[8];
    char c[8] = "abcdef";

    flare_memcpy(b, "xyz", 3);
    CHECK(b[0] == 'x' && b[2] == 'z');

    flare_memset(b, 'q', 3);
    CHECK(b[0] == 'q' && b[2] == 'q');

    flare_memmove(a + 2, a, 4);
    CHECK(flare_memcmp(a, "ababcd", 6) == 0);

    flare_memmove(c, c + 2, 4);
    CHECK(flare_memcmp(c, "cdefef", 6) == 0);

    CHECK(flare_memcmp("abc", "abd", 3) < 0);
    CHECK(flare_memcmp("abc", "abc", 3) == 0);
    CHECK(flare_memcmp("abd", "abc", 3) > 0);
}

static void test_strings(void) {
    char out[4];

    CHECK(flare_strlen("hello") == 5);
    CHECK(flare_strlen("") == 0);
    CHECK(flare_strcmp("abc", "abc") == 0);
    CHECK(flare_strcmp("abc", "abd") < 0);
    CHECK(flare_strcmp("b", "a") > 0);
    CHECK(flare_strcmp("ab", "abc") < 0);

    CHECK(flare_strlcpy(out, "hello", sizeof out) == 5);
    CHECK(flare_strcmp(out, "hel") == 0);
    CHECK(flare_strlcpy(out, "hi", sizeof out) == 2);
    CHECK(flare_strcmp(out, "hi") == 0);
    CHECK(flare_strlcpy(out, "x", 0) == 1);
}

static void test_numbers(void) {
    char s[16];

    CHECK(flare_utoa(0, s) == 1 && flare_strcmp(s, "0") == 0);
    CHECK(flare_utoa(1000, s) == 4 && flare_strcmp(s, "1000") == 0);
    CHECK(flare_utoa(4294967295u, s) == 10 && flare_strcmp(s, "4294967295") == 0);
    CHECK(flare_itoa(-42, s) == 3 && flare_strcmp(s, "-42") == 0);
    CHECK(flare_itoa(-2147483647 - 1, s) == 11 && flare_strcmp(s, "-2147483648") == 0);
    CHECK(flare_itoa(7, s) == 1 && flare_strcmp(s, "7") == 0);
}

static void test_heap(void) {
    static unsigned char arena[32] __attribute__((aligned(8)));
    flare_heap_t h;
    unsigned char *p1;
    unsigned char *p2;
    unsigned char *p3;

    flare_heap_init(&h, arena, sizeof arena);
    p1 = flare_heap_alloc(&h, 5);
    p2 = flare_heap_alloc(&h, 5);
    CHECK(p1 == arena && p2 == arena + 8);
    CHECK(flare_heap_free_bytes(&h) == 19);
    CHECK(flare_heap_alloc(&h, 20) == 0);

    p3 = flare_heap_alloc(&h, 16);
    CHECK(p3 == arena + 16);
    CHECK(flare_heap_alloc(&h, 1) == 0);

    flare_heap_reset(&h);
    CHECK(flare_heap_free_bytes(&h) == 32);

    flare_memset(arena, 0xAA, sizeof arena);
    p1 = flare_heap_zalloc(&h, 8);
    CHECK(p1 == arena && p1[0] == 0 && p1[7] == 0);
}

int main(void) {
    test_memory();
    test_strings();
    test_numbers();
    test_heap();

    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("OK: 0 failure(s)\n");
    return 0;
}
