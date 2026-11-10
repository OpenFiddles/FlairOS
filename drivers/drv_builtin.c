/*
FILE PATH: drivers/drv_builtin.c
CONTRIBUTOR (Optional): Textile
PURPOSE: Builtin driver table. Registers every built-in driver in boot order, including the polling ATA PIO disk driver.
*/

#include "../include/drv.h"
#include "../include/drv_pic.h"
#include "../include/drv_timer.h"
#include "../include/drv_serial.h"
#include "../include/drv_kbd.h"
#include "../include/drv_rtc.h"
#include "../include/drv_speaker.h"
#include "../include/drv_net.h"
#include "../include/drv_ata.h"

static const struct drv_ops drv_ata_ops = { ata_init, 0, 0 };
static struct drv drv_ata_device = { "ata", DRV_CLASS_CHAR, &drv_ata_ops, DRV_NO_IRQ, 0 };

/* Order matters: the interrupt controller must come before anything that unmasks an IRQ. */
int drv_register_builtin(void) {
    struct drv *list[] = {
        &drv_pic_device,
        &drv_timer_device,
        &drv_serial_device,
        &drv_kbd_device,
        &drv_rtc_device,
        &drv_speaker_device,
        &drv_net_device,
        &drv_ata_device,
    };
    int ok = 0;
    for (unsigned int i = 0; i < sizeof(list) / sizeof(list[0]); i++)
        if (drv_register(list[i]) == 0)
            ok++;
    return ok;
}
