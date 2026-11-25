#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "setjmp.h"
#include "inttypes.h"
#include "math.h"
#include "unistd.h"
#include "fcntl.h"
#include "sys/stat.h"
#include "dirent.h"
#include "signal.h"
#include "locale.h"
#include "float.h"
#include "flairos_pyconfig.h"

int main(void) {
    jmp_buf jb;
    struct stat st;
    (void)stat("/home/NOTE.TXT", &st);
    if (setjmp(jb) == 0) {
        long v = strtol("42", 0, 10);
        printf("FlairOS CPython SDK Ready: %" PRId32 "\n", (int)v);
    }
    return 0;
}
