#include <stdio.h>
#include <string.h>
#include "flare_stdlib.h"

static int passed = 0, failed = 0;

static void check(int ok, const char *name) {
    if (ok) {
        passed++;
    } else {
        failed++;
        printf("FAIL %s\n", name);
    }
}

static int cmp_int(const void *a, const void *b) {
    int x = *(const int *)a, y = *(const int *)b;
    return (x > y) - (x < y);
}

#define FMT_CHECK(name, ...) do { \
    char a_[128], b_[128]; \
    int ra_ = flare_snprintf(a_, sizeof a_, __VA_ARGS__); \
    int rb_ = snprintf(b_, sizeof b_, __VA_ARGS__); \
    check(ra_ == rb_ && strcmp(a_, b_) == 0, name); \
} while (0)

int main(void) {
    char d[8];
    char c[16] = "foo";
    const char *s = "hello world";
    char *end = 0;
    int arr[] = {5, 1, 4, 2, 3};
    int key = 3, missing = 9;

    FMT_CHECK("fmt int", "%d", 42);
    FMT_CHECK("fmt neg", "%d", -17);
    FMT_CHECK("fmt int min", "%d", (-2147483647 - 1));
    FMT_CHECK("fmt width", "%5d|", 42);
    FMT_CHECK("fmt left", "%-5d|", 42);
    FMT_CHECK("fmt zero neg", "%05d", -42);
    FMT_CHECK("fmt prec", "%.3d", 7);
    FMT_CHECK("fmt hex", "%x", 255);
    FMT_CHECK("fmt HEX", "%X", 255);
    FMT_CHECK("fmt hex pad", "%08x", 0xbeef);
    FMT_CHECK("fmt octal", "%o", 8);
    FMT_CHECK("fmt unsigned", "%u", 4000000000u);
    FMT_CHECK("fmt long", "%ld", 123456789L);
    FMT_CHECK("fmt ulong", "%lu", 42UL);
    FMT_CHECK("fmt str", "%s", "hello");
    FMT_CHECK("fmt str width", "%10s|", "abc");
    FMT_CHECK("fmt str left", "%-10s|", "abc");
    FMT_CHECK("fmt str prec", "%.2s", "abcdef");
    FMT_CHECK("fmt char", "%c", 'Z');
    FMT_CHECK("fmt percent", "100%%");
    FMT_CHECK("fmt mixed", "%s=%d (%x) %c%%", "k", -3, 255, 'z');
    {
        char t[8];
        int r = flare_snprintf(t, sizeof t, "%s", "abcdefghijklmnop");
        check(r == 16 && strcmp(t, "abcdefg") == 0, "snprintf truncates, returns full length");
    }

    flare_strncpy(d, "ab", 5);
    check(d[0] == 'a' && d[1] == 'b' && d[2] == 0 && d[3] == 0 && d[4] == 0, "strncpy pads");
    check(flare_strncmp("abc", "abd", 3) < 0 && flare_strncmp("abc", "abd", 2) == 0, "strncmp");
    check(flare_strchr(s, 'l') == s + 2, "strchr");
    check(flare_strchr(s, 'q') == 0, "strchr miss");
    check(flare_strrchr(s, 'o') == s + 7, "strrchr");
    check(flare_strstr(s, "wor") == s + 6, "strstr");
    check(flare_strstr(s, "xyz") == 0, "strstr miss");
    check(flare_strstr(s, "") == s, "strstr empty needle");
    flare_strcat(c, "bar");
    check(strcmp(c, "foobar") == 0, "strcat");
    flare_strncat(c, "bazqux", 3);
    check(strcmp(c, "foobarbaz") == 0, "strncat");
    check(flare_memchr(s, 'w', 11) == s + 6, "memchr");

    check(flare_isdigit('5') && !flare_isdigit('a'), "isdigit");
    check(flare_isspace('\t') && flare_isspace(' ') && !flare_isspace('x'), "isspace");
    check(flare_isalpha('Q') && !flare_isalpha('1'), "isalpha");
    check(flare_toupper('a') == 'A' && flare_tolower('Q') == 'q', "toupper/tolower");
    check(flare_isxdigit('f') && !flare_isxdigit('g'), "isxdigit");

    check(flare_atoi(" -123xyz") == -123, "atoi");
    flare_errno = 0;
    check(flare_strtol("0x1F", &end, 0) == 31 && *end == 0, "strtol hex base 0");
    check(flare_strtol("0777", 0, 0) == 511, "strtol octal base 0");
    check(flare_strtol("zz", &end, 36) == 1295 && *end == 0, "strtol base 36");
    flare_errno = 0;
    check(flare_strtol("99999999999999999999", 0, 10) == FLARE_LONG_MAX && flare_errno == FLARE_ERANGE,
          "strtol overflow sets ERANGE");
    flare_errno = 0;
    check(flare_strtoul("42", 0, 10) == 42 && flare_errno == 0, "strtoul");
    check(flare_abs(-5) == 5 && flare_labs(-7L) == 7L, "abs labs");

    flare_qsort(arr, 5, sizeof arr[0], cmp_int);
    check(arr[0] == 1 && arr[1] == 2 && arr[2] == 3 && arr[3] == 4 && arr[4] == 5, "qsort");
    check(flare_bsearch(&key, arr, 5, sizeof arr[0], cmp_int) == &arr[2], "bsearch hit");
    check(flare_bsearch(&missing, arr, 5, sizeof arr[0], cmp_int) == 0, "bsearch miss");

    printf("libc host: %d pass, %d fail\n", passed, failed);
    return failed ? 1 : 0;
}
