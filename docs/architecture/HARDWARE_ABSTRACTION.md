# Hardware abstraction

The HAL exists so kernel subsystems can be written once. `include/hal.h` is the
contract; `arch/<name>/hal.c` implements it.

## HAL surface

| Group | Functions | Purpose |
| --- | --- | --- |
| Architecture | `hal_early_init`, `hal_arch_name` | Earliest bring-up and identification |
| Port I/O | `hal_port_read8/16/32`, `hal_port_write8/16/32`, `hal_io_wait` | x86 `in`/`out`; returns an error on architectures without port I/O |
| MMIO | `hal_mmio_read8/32`, `hal_mmio_write8/32` | Memory-mapped device access |
| Serial | `hal_serial_putc`, `hal_serial_puts` | Debug console |
| Time | `hal_timer_freq_hz`, `hal_timer_ticks` | Monotonic ticks |
| Interrupts | `hal_cpu_irq_enable/disable`, `hal_cpu_halt` | CPU control |
| Bridge | `hal_bridge_init`, `hal_bridge_putc`, `hal_bridge_getc` | Host or emulator console bridge |
| Power | `hal_power_shutdown` | Shutdown, forced or graceful |
| Form factor | `hal_is_mobile_form_factor` | Layout hint |

A `hal_port_read*` call on an architecture without port I/O returns failure
rather than silently reading zero. Callers must handle it.

## Why the HAL is thin

The HAL does not abstract devices, only CPU and bus primitives. Drivers call HAL
for port I/O and MMIO, then implement their own device logic once. Abstracting
whole devices would mean designing an interface for hardware that does not exist
yet, which tends to produce abstractions that fit nothing.

## Implemented ports

| Port | File | Completeness |
| --- | --- | --- |
| i386 | `legacy/i386/arch/hal.c` | Complete; the working target |
| AArch64 | `arch/aarch64/hal.c` | Bring-up: serial, timer, halt. Port I/O returns failure |
| x86_64 | `arch/x86_64/hal.c` | Present but not a supported build target |

## i386 drivers

| Driver | File | Notes |
| --- | --- | --- |
| ATA PIO | `drivers/drv_ata.c` | Primary disk, sector reads and writes |
| PS/2 keyboard | `drivers/drv_kbd.c` | Scancode set 1, IRQ1 |
| PS/2 mouse | `drivers/mouse_init.c` | IRQ12, click and drag |
| PIT | `drivers/pit_timer.c` | IRQ0, scheduler tick |
| RTC | `drivers/drv_rtc.c` | CMOS clock, ports 0x70/0x71 |
| Serial | `drivers/drv_serial.c` | COM1, mirrored for tests |
| Speaker | `drivers/drv_speaker.c` | PC speaker tones |
| PIC | `drivers/drv_pic.c` | 8259 remap |
| RTL8139 | `drivers/net_rtl8139.c` | Detection, MAC, MTU, link, ICMP |
| NE2000 | `drivers/net/ne2000/drv.c` | Alternate network device |
| Touch | `drivers/drv_touch.c` | Pointer mapping |

### Driver registration

`drivers/drv_core.c` provides a registration framework: drivers register
name, init and self-test callbacks, and `drv_init_all` runs them in order. A
driver that fails its self-test reports and is skipped; it does not halt boot.

This matters because the system must still reach a console on hardware with a
non-functional peripheral. The exception is anything the session cannot function
without: the framebuffer and the display, which fail loudly.

## Firmware interfaces

| Interface | Use | Where |
| --- | --- | --- |
| BIOS `int 0x13` | Disk sector read with CHS | `stage1.asm` |
| BIOS `int 0x15` (E820) | Memory map | `stage2.asm` |
| BIOS `int 0x15` (A20) | A20 gate | `stage2.asm` |
| Bochs DISPI | Linear framebuffer | `kernel/gfx/vbe.c` |
| PCI config ports | Device discovery | `kernel/boot/hwinfo.c` |
| QEMU `fw_cfg` | ramfb setup (AArch64, not built) | `arch/aarch64/ramfb.c` |

Hardware inventory (`kernel/boot/hwinfo.c`) walks the PCI bus and records up to 16
devices (`HWINFO_MAX_PCI`). The limit is static because inventory runs before the
heap exists.

## Adding a driver

1. Implement the register/init/self-test structure used by `drivers/drv_core.c`.
2. Read hardware only through HAL port or MMIO calls.
3. Put anything needed before heap init in a bounded static array.
4. Log detection and failure to serial, and return a status instead of panicking
   unless the session cannot start without the device.
5. Add a host test for the pure logic if the driver has any.
