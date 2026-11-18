# FlairOS Hardware Abstraction Layer

## Purpose

The HAL is the boundary between shared kernel services and CPU/platform-specific
mechanisms. It is not a device-driver framework and does not make an unsupported
board bootable by itself.

## Current implementation

- `include/hal.h` defines CPU-control and legacy port-I/O contracts.
- `arch/i386/hal.c` implements the contract for the current i386 target.
- `arch/aarch64/hal.c` provides AArch64 CPU interrupt/wait primitives and
  explicitly rejects x86-style port I/O. It is groundwork, not a complete
  mobile or AArch64 platform port.
- Existing PC drivers can use `include/hw.h` compatibility wrappers while
  they are migrated to explicit HAL/device interfaces.

## Required next layers for AArch64 phones

A phone port needs substantially more than CPU instructions:

1. A boot protocol and board/SoC platform description (typically Device Tree).
2. Early UART/console support for the target board.
3. GIC interrupt-controller support and interrupt routing.
4. Generic architectural timer support.
5. AArch64 MMU/page-table implementation and cache/TLB maintenance.
6. Physical memory discovery and reservations from the platform handoff.
7. PSCI or platform-defined CPU power/reset operations.
8. SoC-specific clocks, pinctrl, regulators, storage, USB, display, touch,
   audio, Wi-Fi and battery drivers as applicable.

The shared kernel should consume stable interfaces for interrupts, timers,
physical memory, virtual memory, console, block devices and input. It must not
directly depend on x86 PIC/PIT/VGA/BIOS mechanisms.

## Portability rules

- Architecture code belongs under `arch/<architecture>/`.
- Board-specific initialization belongs under a platform/SoC layer, not in
  generic kernel services.
- A capability that does not exist on a platform must return an explicit unsupported result; it must not silently emulate unrelated hardware.
- Keep the existing i386 BIOS image working while the new platform path is
  developed and tested independently.
- Do not advertise ARM64 or phone support until a real board boots and its
  timer, interrupt controller, memory map, console and storage are validated.
