# FlareOS freestanding C library

The SDK provides a small freestanding C header layer under `sdk/`. Compile apps for the FlareOS i386 ABI and add `-Isdk` to the app compiler include path.

## Available interfaces

- `string.h`: `memset`, `memcpy`, `memmove`, `memcmp`, `strlen`, `strcmp`, and `strlcpy`.
- `stdint.h`: fixed-width integer types and common limits for the current 32-bit i386 ABI.
- `stdbool.h`: `bool`, `true`, and `false`.
- `stddef.h`: `size_t`, `ptrdiff_t`, `NULL`, and `offsetof`.
- `stdlib.h`: `abs`, `atoi`, `malloc`, `calloc`, `realloc`, `free`, and success/failure constants.

## Heap allocator

The app runtime implements an 8-byte-aligned, first-fit heap allocator backed by the app's private `SYS_BRK` region. It supports splitting and coalescing free blocks, zero-initialized `calloc`, and preserving reallocations. The VM currently provides a 4 MiB heap window per app; allocation can fail and callers must check for NULL. The allocator is single-threaded, matching the current single-foreground-app runtime. Invalid or repeated frees are ignored when detectable; passing arbitrary pointers to `free` remains undefined behavior.

## Scope and limitations

This is a freestanding subset, not a complete hosted ISO C/POSIX library. There is not yet a standard `stdio.h`, `printf`, process environment, locale, or POSIX file descriptor layer. File access remains available through the FlareOS `flare_fs_*` / `flare_f*` interfaces. Do not put `-Isdk` ahead of host system headers when compiling host tools: these SDK headers target the 32-bit app ABI.
