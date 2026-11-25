#ifndef FLAIROS_PYCONFIG_H
#define FLAIROS_PYCONFIG_H

/* CPython configuration for FlairOS freestanding SDK/libc */
#define Py_BUILD_CORE 1
#define SIZEOF_INT 4
#define SIZEOF_LONG 4
#define SIZEOF_LONG_LONG 8
#define SIZEOF_VOID_P 4
#define SIZEOF_SIZE_T 4
#define SIZEOF_OFF_T 4
#define SIZEOF_TIME_T 4
#define SIZEOF_FLOAT 4
#define SIZEOF_DOUBLE 8
#define HAVE_STDINT_H 1
#define HAVE_STDDEF_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_ERRNO_H 1
#define HAVE_FCNTL_H 1
#define HAVE_UNISTD_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_DIRENT_H 1
#define HAVE_SIGNAL_H 1
#define HAVE_SNPRINTF 1

#endif
