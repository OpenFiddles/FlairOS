#ifndef FLAIROS_BROWSER_H
#define FLAIROS_BROWSER_H

#include <stdint.h>
#include "../../include/airgraph.h"

void flair_browser_init(void);
void flair_browser_navigate(const char *url);
int flair_browser_handle_click(int32_t rx, int32_t ry, int32_t w, int32_t h);
int flair_browser_handle_key(uint16_t key);
void flair_browser_render(struct ag_surface *s, int32_t wx, int32_t wy, int32_t ww, int32_t wh);
const char *flair_browser_current_url(void);
int flair_browser_node_count(void);

#endif
