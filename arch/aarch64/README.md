# FlairOS AArch64 native port

Independent native ARM64 target; it is not v86 and does not reuse the i386 boot chain.

Build:
```sh
make -C arch/aarch64 CROSS=aarch64-linux-gnu-
```

Smoke:
```sh
qemu-system-aarch64 -M virt -cpu cortex-a72 -m 128M -nographic -nic none -kernel arch/aarch64/kernel.elf
```

Initial scaffold: QEMU `-kernel` entry, masked interrupts, BSS clearing, 64 KiB stack, C entry, PL011 UART. Not yet a full OS: exception vectors, MMU/page tables, GIC, timer, scheduler, drivers, and graphics remain future work.
