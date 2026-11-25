#ifndef FLAIROS_PYTHON_H
#define FLAIROS_PYTHON_H

#include <stdint.h>

typedef void (*flair_py_emit_fn)(const char *line, void *ctx);

void flair_py_init(void);
int flair_py_eval_line(const char *line, flair_py_emit_fn emit, void *ctx);
int flair_py_run_script(const char *script, flair_py_emit_fn emit, void *ctx);
int flair_py_run_file(const char *vfs_path, flair_py_emit_fn emit, void *ctx);

#endif
