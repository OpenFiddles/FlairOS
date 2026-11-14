# FlareOS freestanding C library

The SDK provides a small freestanding C header layer under `sdk/`. Add `-Isdk` to the compiler include path and include familiar headers such as `<stdint.h>`, `<stdbool.h>`, `<stddef.h>`, `<string.h>`, and `<stdlib.h>`.

## Available interfaces

- `string.h`: `memset`, `memcpy`, `memmove`, `memcmp`, `strlen`, `strcmp`, and `strlcpy`.
- `stdint.h`: fixed-width integer types and common limits for the current 32-bit i386 ABI.
- `stdbool.h`: `bool`, `true`, and `false`.
- `stddef.h`: `size_t`, `ptrdiff_t`, `NULL`, and `offsetof`.
- `stdlib.h`: `abs`, `atoi`, and success/failure constants.

The existing `flareos.h`, `flare_string.h`, and `flare_libc.h` remain supported. This is not yet a complete hosted ISO C library: there is no general-purpose `malloc/free`, `printf`, process environment, locale, POSIX layer, or standard file descriptor API. File access currently uses the FlareOS `flare_fs_*` / `flare_f*` interfaces.

These headers are for the freestanding i386 application ABI. They are not drop-in replacements for the host system's C library. The SDK must not be added ahead of system headers when compiling host tools.
