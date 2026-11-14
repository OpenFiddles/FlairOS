#include <stddef.h>
#include <stdint.h>
#include "py/mpconfig.h"
#include "py/obj.h"
#include "py/objlist.h"
#include "py/objmodule.h"
#include "py/objstr.h"
#include "py/mperrno.h"
#include "py/misc.h"
#include "py/runtime.h"
#include "py/lexer.h"
#include "py/parse.h"
#include "py/compile.h"
#include "flareos_abi.h"

static const char *checked_name(mp_obj_t obj) {
    const char *name = mp_obj_str_get_str(obj);
    size_t len = 0;
    while (name[len]) {
        if (name[len] == '/') mp_raise_ValueError(MP_ERROR_TEXT("FlareOS files use flat names"));
        ++len;
    }
    if (len == 0 || len > FS_NAME_MAX)
        mp_raise_ValueError(MP_ERROR_TEXT("filename must be 1-12 characters"));
    return name;
}
static mp_obj_t flareos_ticks_ms(void) {
    return mp_obj_new_int_from_uint(flareos_call(SYS_TICKS, 0, 0, 0) * 10u);
}
static MP_DEFINE_CONST_FUN_OBJ_0(flareos_ticks_ms_obj, flareos_ticks_ms);
static mp_obj_t flareos_sleep_ms(mp_obj_t arg) {
    mp_int_t ms = mp_obj_get_int(arg);
    if (ms < 0) mp_raise_ValueError(MP_ERROR_TEXT("ms must be non-negative"));
    if (ms) flareos_call(SYS_SLEEP, (uint32_t)(((uint32_t)ms + 9u) / 10u), 0, 0);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(flareos_sleep_ms_obj, flareos_sleep_ms);
static mp_obj_t flareos_beep(mp_obj_t hz_obj, mp_obj_t ms_obj) {
    mp_int_t hz = mp_obj_get_int(hz_obj), ms = mp_obj_get_int(ms_obj);
    if (hz < 20 || hz > 20000 || ms < 1 || ms > 5000)
        mp_raise_ValueError(MP_ERROR_TEXT("beep expects 20-20000 Hz and 1-5000 ms"));
    flareos_call(SYS_TONE, (uint32_t)hz, (uint32_t)ms, 0);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(flareos_beep_obj, flareos_beep);
static mp_obj_t flareos_getkey(void) {
    int key = (int)flareos_call(SYS_GETKEY, 0, 0, 0);
    return key < 0 ? mp_const_none : mp_obj_new_int(key);
}
static MP_DEFINE_CONST_FUN_OBJ_0(flareos_getkey_obj, flareos_getkey);
static mp_obj_t flareos_screen(void) {
    uint32_t packed = flareos_call(SYS_SCREEN, 0, 0, 0);
    mp_obj_t dims[2] = {
        mp_obj_new_int(packed & 0xFFu),
        mp_obj_new_int((packed >> 8) & 0xFFu),
    };
    return mp_obj_new_tuple(2, dims);
}
static MP_DEFINE_CONST_FUN_OBJ_0(flareos_screen_obj, flareos_screen);
static mp_obj_t flareos_get_arrow(void) {
    int arrow = (int)flareos_call(SYS_GETARROW, 0, 0, 0);
    return arrow < 0 ? mp_const_none : mp_obj_new_int(arrow);
}
static MP_DEFINE_CONST_FUN_OBJ_0(flareos_get_arrow_obj, flareos_get_arrow);
static mp_obj_t flareos_pointer(void) {
    uint32_t packed = flareos_call(SYS_POINTER, 0, 0, 0);
    mp_obj_t values[3] = {
        mp_obj_new_int(packed & 0xFFu),
        mp_obj_new_int((packed >> 8) & 0xFFu),
        mp_obj_new_int((packed >> 16) & 0x07u),
    };
    return mp_obj_new_tuple(3, values);
}
static MP_DEFINE_CONST_FUN_OBJ_0(flareos_pointer_obj, flareos_pointer);
static mp_obj_t flareos_clear(void) {
    flareos_call(SYS_CLEAR, 0, 0, 0);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(flareos_clear_obj, flareos_clear);
static mp_obj_t flareos_fs_list(void) {
    mp_obj_t result = mp_obj_new_list(0, NULL);
    char name[FS_NAME_MAX + 1u];
    for (uint32_t i = 0; i < FS_FILES; ++i)
        if (fs_list(i, name) != FS_FAIL)
            mp_obj_list_append(result, mp_obj_new_str_from_cstr(name));
    return result;
}
static MP_DEFINE_CONST_FUN_OBJ_0(flareos_fs_list_obj, flareos_fs_list);
static mp_obj_t flareos_fs_open(mp_obj_t name_obj, mp_obj_t mode_obj) {
    mp_int_t mode = mp_obj_get_int(mode_obj);
    uint32_t handle;
    if (mode != (mp_int_t)FS_READ && mode != (mp_int_t)FS_WRITE &&
        mode != (mp_int_t)FS_APPEND)
        mp_raise_ValueError(MP_ERROR_TEXT("mode must be FS_READ, FS_WRITE, or FS_APPEND"));
    handle = fs_open(checked_name(name_obj), (uint32_t)mode);
    if (handle == FS_FAIL)
        mp_raise_OSError(mode == FS_READ ? MP_ENOENT : MP_EIO);
    return mp_obj_new_int_from_uint(handle);
}
static MP_DEFINE_CONST_FUN_OBJ_2(flareos_fs_open_obj, flareos_fs_open);

static mp_obj_t flareos_fs_read_handle(mp_obj_t handle_obj, mp_obj_t count_obj) {
    mp_int_t handle = mp_obj_get_int(handle_obj);
    mp_int_t count = mp_obj_get_int(count_obj);
    byte *buffer;
    uint32_t got;
    mp_obj_t result;
    if (handle <= 0 || handle > FS_HANDLES || count < 0 || count > FS_FILE_MAX)
        mp_raise_ValueError(MP_ERROR_TEXT("invalid handle or read size"));
    buffer = m_new(byte, count ? (size_t)count : 1u);
    got = count ? fs_read((uint32_t)handle, buffer, (uint32_t)count) : 0;
    if (got == FS_FAIL) {
        m_del(byte, buffer, count ? (size_t)count : 1u);
        mp_raise_OSError(MP_EIO);
    }
    result = mp_obj_new_bytes(buffer, got);
    m_del(byte, buffer, count ? (size_t)count : 1u);
    return result;
}
static MP_DEFINE_CONST_FUN_OBJ_2(flareos_fs_read_handle_obj, flareos_fs_read_handle);

static mp_obj_t flareos_fs_write_handle(mp_obj_t handle_obj, mp_obj_t data_obj) {
    mp_int_t handle = mp_obj_get_int(handle_obj);
    size_t size;
    const char *data = mp_obj_str_get_data(data_obj, &size);
    uint32_t wrote;
    if (handle <= 0 || handle > FS_HANDLES || size > FS_FILE_MAX)
        mp_raise_ValueError(MP_ERROR_TEXT("invalid handle or write size"));
    wrote = size ? fs_write((uint32_t)handle, data, (uint32_t)size) : 0;
    if (wrote == FS_FAIL) mp_raise_OSError(MP_EIO);
    return mp_obj_new_int_from_uint(wrote);
}
static MP_DEFINE_CONST_FUN_OBJ_2(flareos_fs_write_handle_obj, flareos_fs_write_handle);

static mp_obj_t flareos_fs_close_handle(mp_obj_t handle_obj) {
    mp_int_t handle = mp_obj_get_int(handle_obj);
    if (handle <= 0 || handle > FS_HANDLES)
        mp_raise_ValueError(MP_ERROR_TEXT("invalid filesystem handle"));
    if (fs_close((uint32_t)handle) == FS_FAIL) mp_raise_OSError(MP_EBADF);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(flareos_fs_close_handle_obj, flareos_fs_close_handle);

static mp_obj_t flareos_fs_seek(mp_obj_t handle_obj, mp_obj_t position_obj) {
    mp_int_t handle = mp_obj_get_int(handle_obj);
    mp_int_t position = mp_obj_get_int(position_obj);
    uint32_t result;
    if (handle <= 0 || handle > FS_HANDLES || position < 0 || position > FS_FILE_MAX)
        mp_raise_ValueError(MP_ERROR_TEXT("invalid handle or file position"));
    result = fs_seek((uint32_t)handle, (uint32_t)position);
    if (result == FS_FAIL) mp_raise_OSError(MP_EINVAL);
    return mp_obj_new_int_from_uint(result);
}
static MP_DEFINE_CONST_FUN_OBJ_2(flareos_fs_seek_obj, flareos_fs_seek);

static mp_obj_t flareos_fs_read(mp_obj_t name_obj) {
    uint32_t handle = fs_open(checked_name(name_obj), FS_READ), size, got;
    byte *buffer;
    mp_obj_t result;
    if (handle == FS_FAIL) mp_raise_OSError(MP_ENOENT);
    size = fs_size(handle);
    if (size == FS_FAIL || size > FS_FILE_MAX) {
        fs_close(handle); mp_raise_OSError(MP_EIO);
    }
    buffer = m_new(byte, size ? size : 1u);
    got = size ? fs_read(handle, buffer, size) : 0;
    fs_close(handle);
    if (got == FS_FAIL || got != size) {
        m_del(byte, buffer, size ? size : 1u); mp_raise_OSError(MP_EIO);
    }
    result = mp_obj_new_bytes(buffer, size);
    m_del(byte, buffer, size ? size : 1u);
    return result;
}
static MP_DEFINE_CONST_FUN_OBJ_1(flareos_fs_read_obj, flareos_fs_read);
static mp_obj_t flareos_fs_write(mp_obj_t name_obj, mp_obj_t data_obj) {
    const char *name = checked_name(name_obj);
    size_t size;
    const char *data = mp_obj_str_get_data(data_obj, &size);
    uint32_t handle, wrote;
    if (size > FS_FILE_MAX) mp_raise_ValueError(MP_ERROR_TEXT("file exceeds 64 KiB"));
    handle = fs_open(name, FS_WRITE);
    if (handle == FS_FAIL) mp_raise_OSError(MP_EIO);
    wrote = size ? fs_write(handle, data, (uint32_t)size) : 0;
    fs_close(handle);
    if (wrote == FS_FAIL || wrote != size) mp_raise_OSError(MP_EIO);
    return mp_obj_new_int_from_uint((mp_uint_t)wrote);
}
static MP_DEFINE_CONST_FUN_OBJ_2(flareos_fs_write_obj, flareos_fs_write);
static mp_obj_t flareos_fs_exists(mp_obj_t name_obj) {
    return mp_obj_new_bool(fs_exists(checked_name(name_obj)) != 0);
}
static MP_DEFINE_CONST_FUN_OBJ_1(flareos_fs_exists_obj, flareos_fs_exists);
static mp_obj_t flareos_fs_remove(mp_obj_t name_obj) {
    if (fs_delete(checked_name(name_obj)) == FS_FAIL) mp_raise_OSError(MP_EIO);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(flareos_fs_remove_obj, flareos_fs_remove);
static mp_obj_t flareos_fs_rename(mp_obj_t old_obj, mp_obj_t new_obj) {
    const char *old_name = checked_name(old_obj);
    const char *new_name = checked_name(new_obj);
    if (fs_rename(old_name, new_name) == FS_FAIL) mp_raise_OSError(MP_EIO);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(flareos_fs_rename_obj, flareos_fs_rename);
static mp_obj_t flareos_fs_slots(void) {
    return mp_obj_new_int_from_uint(flareos_call(SYS_FS_SLOTS, 0, 0, 0));
}
static MP_DEFINE_CONST_FUN_OBJ_0(flareos_fs_slots_obj, flareos_fs_slots);
static mp_obj_t flareos_run(mp_obj_t name_obj) {
    const char *name = checked_name(name_obj);
    uint32_t handle = fs_open(name, FS_READ), size, got;
    char *source;
    mp_lexer_t *lex;
    mp_parse_tree_t parse_tree;
    mp_obj_t module_fun;
    if (handle == FS_FAIL) mp_raise_OSError(MP_ENOENT);
    size = fs_size(handle);
    if (size == FS_FAIL || size > FS_FILE_MAX) {
        fs_close(handle); mp_raise_OSError(MP_EIO);
    }
    source = m_new(char, size + 1u);
    got = size ? fs_read(handle, source, size) : 0;
    fs_close(handle);
    if (got == FS_FAIL || got != size) {
        m_del(char, source, size + 1u); mp_raise_OSError(MP_EIO);
    }
    source[size] = 0;
    lex = mp_lexer_new_from_str_len(MP_QSTR__lt_stdin_gt_, source, size, 0);
    parse_tree = mp_parse(lex, MP_PARSE_FILE_INPUT);
    module_fun = mp_compile(&parse_tree, lex->source_name, false);
    mp_lexer_free(lex);
    mp_call_function_0(module_fun);
    m_del(char, source, size + 1u);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(flareos_run_obj, flareos_run);
static const mp_rom_map_elem_t flareos_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_flareos) },
    { MP_ROM_QSTR(MP_QSTR_ticks_ms), MP_ROM_PTR(&flareos_ticks_ms_obj) },
    { MP_ROM_QSTR(MP_QSTR_sleep_ms), MP_ROM_PTR(&flareos_sleep_ms_obj) },
    { MP_ROM_QSTR(MP_QSTR_beep), MP_ROM_PTR(&flareos_beep_obj) },
    { MP_ROM_QSTR(MP_QSTR_getkey), MP_ROM_PTR(&flareos_getkey_obj) },
    { MP_ROM_QSTR(MP_QSTR_screen), MP_ROM_PTR(&flareos_screen_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_arrow), MP_ROM_PTR(&flareos_get_arrow_obj) },
    { MP_ROM_QSTR(MP_QSTR_pointer), MP_ROM_PTR(&flareos_pointer_obj) },
    { MP_ROM_QSTR(MP_QSTR_clear), MP_ROM_PTR(&flareos_clear_obj) },
    { MP_ROM_QSTR(MP_QSTR_FS_READ), MP_ROM_INT(FS_READ) },
    { MP_ROM_QSTR(MP_QSTR_FS_WRITE), MP_ROM_INT(FS_WRITE) },
    { MP_ROM_QSTR(MP_QSTR_FS_APPEND), MP_ROM_INT(FS_APPEND) },
    { MP_ROM_QSTR(MP_QSTR_fs_open), MP_ROM_PTR(&flareos_fs_open_obj) },
    { MP_ROM_QSTR(MP_QSTR_fs_read_handle), MP_ROM_PTR(&flareos_fs_read_handle_obj) },
    { MP_ROM_QSTR(MP_QSTR_fs_write_handle), MP_ROM_PTR(&flareos_fs_write_handle_obj) },
    { MP_ROM_QSTR(MP_QSTR_fs_close), MP_ROM_PTR(&flareos_fs_close_handle_obj) },
    { MP_ROM_QSTR(MP_QSTR_fs_seek), MP_ROM_PTR(&flareos_fs_seek_obj) },
    { MP_ROM_QSTR(MP_QSTR_fs_list), MP_ROM_PTR(&flareos_fs_list_obj) },
    { MP_ROM_QSTR(MP_QSTR_fs_read), MP_ROM_PTR(&flareos_fs_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_fs_write), MP_ROM_PTR(&flareos_fs_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_fs_exists), MP_ROM_PTR(&flareos_fs_exists_obj) },
    { MP_ROM_QSTR(MP_QSTR_fs_remove), MP_ROM_PTR(&flareos_fs_remove_obj) },
    { MP_ROM_QSTR(MP_QSTR_fs_rename), MP_ROM_PTR(&flareos_fs_rename_obj) },
    { MP_ROM_QSTR(MP_QSTR_fs_slots), MP_ROM_PTR(&flareos_fs_slots_obj) },
    { MP_ROM_QSTR(MP_QSTR_run), MP_ROM_PTR(&flareos_run_obj) },
};
static MP_DEFINE_CONST_DICT(flareos_globals, flareos_globals_table);
const mp_obj_module_t mp_module_flareos = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&flareos_globals,
};
MP_REGISTER_MODULE(MP_QSTR_flareos, mp_module_flareos);
