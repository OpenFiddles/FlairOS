/* arch/aarch64/hello.c - AArch64 HAL + DEX VM bring-up entry */
#include "../../include/hal.h"
#include "../../include/dex.h"

void kmain(void) {
    static const uint16_t sample_insns[] = {
        0x2012u, /* const/4 v0, #2 */
        0x3112u, /* const/4 v1, #3 */
        0x0290u, 0x0100u, /* add-int v2, v0, v1 */
        0x020Fu  /* return v2 */
    };
    int32_t res = 0;
    hal_early_init();
    hal_serial_puts("FlairOS AArch64 HAL ready (");
    hal_serial_puts(hal_arch_name());
    hal_serial_puts(")\n");
    if (dex_exec_bytecode(sample_insns, 5u, &res) == 0 && res == 5)
        hal_serial_puts("FlairOS AArch64 DEX VM verified\n");
    for (;;) hal_cpu_halt();
}
