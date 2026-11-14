#ifndef DRV_H
#define DRV_H

#include "airos_types.h"

/* Generic driver model. Register drivers in order (interrupt controller first), call
   drv_init_all() once with interrupts off, then enable interrupts. An IRQ stub calls
   drv_dispatch_irq(line); that runs the driver's irq hook and sends the end-of-interrupt. */

#define DRV_NAME_MAX 16
#define DRV_MAX 16
#define DRV_IRQ_LINES 16
#define DRV_NO_IRQ (-1)

enum drv_class {
    DRV_CLASS_INTC = 1,
    DRV_CLASS_TIMER,
    DRV_CLASS_CHAR,
    DRV_CLASS_INPUT,
    DRV_CLASS_RTC,
    DRV_CLASS_AUDIO,
    DRV_CLASS_GPU,
    DRV_CLASS_DISPLAY,
    DRV_CLASS_NET,
    DRV_CLASS_MAX = DRV_CLASS_NET
};

struct drv_ops {
    int (*init)(void);
    void (*irq)(void);
    void (*shutdown)(void);
};

struct drv {
    const char *name;
    enum drv_class cls;
    const struct drv_ops *ops;
    int irq_line;
    int ready;
};

int drv_register(struct drv *d);
int drv_init_all(void);
void drv_shutdown_all(void);
void drv_dispatch_irq(int line);
int drv_count(void);
const struct drv *drv_find(const char *name);
const struct drv *drv_find_class(enum drv_class cls);
int drv_register_builtin(void);

#endif
