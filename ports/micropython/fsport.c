#include <stddef.h>
#include "py/lexer.h"
#include "py/misc.h"
#include "py/mperrno.h"
#include "py/runtime.h"
#include "flareos_abi.h"

/* MicroPython's file-execution helper needs a lexer factory when POSIX/VFS
   readers are disabled. Back it with FlareOS's native flat-file filesystem. */
mp_lexer_t *mp_lexer_new_from_file(qstr filename) {
    const char *source_name = qstr_str(filename);
    size_t name_len = qstr_len(filename);
    char name[FS_NAME_MAX + 1u];
    size_t alloc_size;
    uint32_t handle, file_size, got, closed;

    if (name_len == 0 || name_len > FS_NAME_MAX)
        mp_raise_OSError(MP_EINVAL);
    for (size_t i = 0; i < name_len; ++i) {
        if (source_name[i] == '/' || source_name[i] == '\\')
            mp_raise_OSError(MP_EINVAL);
        name[i] = source_name[i];
    }
    name[name_len] = 0;

    handle = fs_open(name, FS_READ);
    if (handle == FS_FAIL)
        mp_raise_OSError(MP_ENOENT);
    file_size = fs_size(handle);
    if (file_size == FS_FAIL || file_size > FS_FILE_MAX) {
        (void)fs_close(handle);
        mp_raise_OSError(MP_EIO);
    }

    alloc_size = file_size ? (size_t)file_size : 1u;
    char *buffer = m_new(char, alloc_size);
    got = file_size ? fs_read(handle, buffer, file_size) : 0u;
    closed = fs_close(handle);
    if (got == FS_FAIL || closed == FS_FAIL) {
        m_del(char, buffer, alloc_size);
        mp_raise_OSError(MP_EIO);
    }

    return mp_lexer_new_from_str_len(filename, buffer, (size_t)got, alloc_size);
}
