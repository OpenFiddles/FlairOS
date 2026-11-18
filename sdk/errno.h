#ifndef FLARE_C_ERRNO_H
#define FLARE_C_ERRNO_H
#include "flare_stdlib.h"
/* errno is private to each app translation unit; FlairOS has no threads yet. */
#define errno flare_errno
#define EDOM FLARE_EDOM
#define ERANGE FLARE_ERANGE
#define EINVAL FLARE_EINVAL
#endif
