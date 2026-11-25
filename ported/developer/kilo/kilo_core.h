#ifndef FLAIROS_KILO_CORE_H
#define FLAIROS_KILO_CORE_H

#define KILO_MAX_TEXT 1024

struct kilo_state {
    char path[128];
    char buf[KILO_MAX_TEXT];
    int len;
    int cursor;
    int dirty;
    char status[64];
};

void kilo_init(struct kilo_state *ks);
int kilo_open_file(struct kilo_state *ks, const char *path);
int kilo_save_file(struct kilo_state *ks);
int kilo_handle_key(struct kilo_state *ks, int key);

#endif
