/* SPDX-License-Identifier: AGPL-3.0-only */
/* FlairOS CPython 3.16 Runtime Bridge (real CPython when linked, no fake calculator) */
#include "flair_py.h"
#include "../../include/vfs.h"

#ifdef FLAIROS_HAS_CPYTHON
#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <string.h>

static int g_py_inited = 0;

static void emit_lines(const char *txt, flair_py_emit_fn emit, void *ctx) {
    if (!txt || !emit) return;
    char line[256];
    int li = 0;
    for (int i = 0; txt[i]; i++) {
        char c = txt[i];
        if (c == '\r') continue;
        if (c == '\n') {
            line[li] = 0;
            if (li > 0) emit(line, ctx);
            li = 0;
        } else if (li + 1 < (int)sizeof(line)) {
            line[li++] = c;
        }
    }
    if (li > 0) {
        line[li] = 0;
        emit(line, ctx);
    }
}

void flair_py_init(void) {
    if (g_py_inited) return;
    PyConfig config;
    PyConfig_InitIsolatedConfig(&config);
    config.site_import = 0;
    config.parse_argv = 0;
    config.module_search_paths_set = 1;
    PyWideStringList_Append(&config.module_search_paths,
        L"/data/data/com.termux/files/home/FlairOS/ported/python/cpython/Lib");
    PyStatus st = Py_InitializeFromConfig(&config);
    PyConfig_Clear(&config);
    if (PyStatus_Exception(st)) return;

    const char *boot =
        "import sys, io, traceback\n"
        "def _flairos_exec(src, single=False):\n"
        "    buf = io.StringIO()\n"
        "    old_out, old_err = sys.stdout, sys.stderr\n"
        "    sys.stdout = sys.stderr = buf\n"
        "    rc = 0\n"
        "    try:\n"
        "        code = compile(src, '<flairos>', 'single' if single else 'exec')\n"
        "        exec(code, globals())\n"
        "    except Exception:\n"
        "        traceback.print_exc()\n"
        "        rc = -1\n"
        "    finally:\n"
        "        sys.stdout, sys.stderr = old_out, old_err\n"
        "    return (rc, buf.getvalue())\n";
    (void)PyRun_SimpleString(boot);
    g_py_inited = 1;
}

static int run_cpython(const char *src, int single, flair_py_emit_fn emit, void *ctx) {
    flair_py_init();
    if (!g_py_inited || !src) return -1;
    PyObject *main_mod = PyImport_AddModule("__main__");
    if (!main_mod) return -1;
    PyObject *fn = PyObject_GetAttrString(main_mod, "_flairos_exec");
    if (!fn) return -1;
    PyObject *res = PyObject_CallFunction(fn, "si", src, single ? 1 : 0);
    Py_DECREF(fn);
    if (!res) {
        PyErr_Print();
        return -1;
    }
    int rc = -1;
    const char *out = "";
    if (PyTuple_Check(res) && PyTuple_Size(res) == 2) {
        rc = (int)PyLong_AsLong(PyTuple_GetItem(res, 0));
        PyObject *s = PyTuple_GetItem(res, 1);
        if (PyUnicode_Check(s)) out = PyUnicode_AsUTF8(s);
    }
    emit_lines(out, emit, ctx);
    Py_DECREF(res);
    return rc;
}

int flair_py_eval_line(const char *line, flair_py_emit_fn emit, void *ctx) {
    return run_cpython(line, 1, emit, ctx);
}

int flair_py_run_script(const char *script, flair_py_emit_fn emit, void *ctx) {
    return run_cpython(script, 0, emit, ctx);
}

int flair_py_run_file(const char *vfs_path, flair_py_emit_fn emit, void *ctx) {
    int fd = vfs_open(vfs_path, VFS_O_RDONLY);
    if (fd < 0) return -1;
    static char buf[8192];
    int n = vfs_read(fd, buf, (uint32_t)(sizeof(buf) - 1));
    vfs_close(fd);
    if (n <= 0) return -1;
    buf[n] = 0;
    return run_cpython(buf, 0, emit, ctx);
}

#else

void flair_py_init(void) {}

int flair_py_eval_line(const char *line, flair_py_emit_fn emit, void *ctx) {
    (void)line;
    if (emit) emit("CPython 3.16: bare-metal kernel lacks full POSIX/libm; run via host-tests", ctx);
    return -1;
}

int flair_py_run_script(const char *script, flair_py_emit_fn emit, void *ctx) {
    return flair_py_eval_line(script, emit, ctx);
}

int flair_py_run_file(const char *vfs_path, flair_py_emit_fn emit, void *ctx) {
    (void)vfs_path;
    return flair_py_eval_line("", emit, ctx);
}

#endif
