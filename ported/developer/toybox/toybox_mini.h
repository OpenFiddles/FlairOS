#ifndef FLAIROS_TOYBOX_MINI_H
#define FLAIROS_TOYBOX_MINI_H

typedef void (*toybox_emit_fn)(const char *line, void *ctx);

int toybox_run_cmd(int argc, const char **argv, const char *cwd,
                   toybox_emit_fn emit, void *ctx);

#endif
