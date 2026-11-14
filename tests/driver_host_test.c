#include <stdio.h>
#include "drv.h"

static int init_ok(void) { return 0; }
static int init_fail(void) { return -1; }
void drv_pic_eoi(int line) { (void)line; }

int main(void) {
    static const struct drv_ops good_ops = { init_ok, 0, 0 };
    static const struct drv_ops bad_ops = { init_fail, 0, 0 };
    static struct drv gpu = { "test-gpu", DRV_CLASS_GPU, &good_ops, DRV_NO_IRQ, 0 };
    static struct drv display = { "test-display", DRV_CLASS_DISPLAY, &good_ops, DRV_NO_IRQ, 0 };
    static struct drv broken = { "bad-class", (enum drv_class)99, &good_ops, DRV_NO_IRQ, 0 };
    static struct drv offline = { "offline-gpu", DRV_CLASS_GPU, &bad_ops, DRV_NO_IRQ, 0 };
    int fails = 0;

#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
    CHECK(drv_register(&gpu) == 0);
    CHECK(drv_register(&display) == 0);
    CHECK(drv_register(&broken) != 0);
    CHECK(drv_register(&offline) == 0);
    CHECK(drv_register(&gpu) != 0);
    CHECK(drv_init_all() == 2);
    CHECK(drv_find_class(DRV_CLASS_GPU) == &gpu);
    CHECK(drv_find_class(DRV_CLASS_DISPLAY) == &display);
    CHECK(drv_find_class(DRV_CLASS_AUDIO) == 0);
    drv_shutdown_all();
    CHECK(drv_find_class(DRV_CLASS_GPU) == 0);
    if (!fails)
        puts("Generic driver host test: all checks passed");
    return fails ? 1 : 0;
}
