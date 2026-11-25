#ifndef FLAIROS_INPUT64_H
#define FLAIROS_INPUT64_H

#include <stdint.h>

#define INPUT64_KEY_ESC   27
#define INPUT64_KEY_ENTER '\n'
#define INPUT64_KEY_TAB   '\t'
#define INPUT64_KEY_UP    0x101
#define INPUT64_KEY_DOWN  0x102
#define INPUT64_KEY_LEFT  0x103
#define INPUT64_KEY_RIGHT 0x104

struct input64_state {
    int32_t mouse_x;
    int32_t mouse_y;
    uint8_t buttons;
    uint8_t clicked;
    int32_t key;
};

void input64_init(int32_t screen_w, int32_t screen_h);
void input64_poll(struct input64_state *out);

#endif
