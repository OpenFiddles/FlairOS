ARCH ?= i386
SUPPORTED_ARCHES := i386

ifeq ($(filter $(ARCH),$(SUPPORTED_ARCHES)),)
$(error ARCH=$(ARCH) has no boot layer yet. Only i386 builds today. x86-64 and ARM need their own boot code first)
endif

# Add one block per architecture when its boot layer lands.
ifeq ($(ARCH),i386)
ARCH_CFLAGS  = -m32
ARCH_ASFLAGS = -f elf32
ARCH_LDFLAGS = -m elf_i386
endif

# Override GNU make's built-in CC=cc default while preserving command-line overrides.
CC = gcc
AS = nasm
LD = ld
OBJCOPY = objcopy
QEMU ?= qemu-system-i386
BUILD = build
DEBUG ?= 0
BOOT_MODE ?= 0

DRV_SRCS = $(shell find drivers -type f -name '*.c' -print)
DRV_OBJS = $(patsubst drivers/%.c,$(BUILD)/d_%.o,$(DRV_SRCS))

DISPLAY ?= landscape
ifeq ($(DISPLAY),portrait)
CFLAGS_DISPLAY = -DFLAIROS_DISPLAY_PORTRAIT
else ifeq ($(DISPLAY),wide)
CFLAGS_DISPLAY = -DFLAIROS_DISPLAY_WIDE
else ifeq ($(DISPLAY),4k)
CFLAGS_DISPLAY = -DFLAIROS_DISPLAY_4K
else
CFLAGS_DISPLAY =
endif

# Keep project headers discoverable for every kernel/driver translation unit.
ifeq ($(DEBUG),1)
CFLAGS_DEBUG = -DFLAIROS_DEBUG_BUILD
endif

CFLAGS = -Iinclude -Ikernel $(CFLAGS_DISPLAY) $(CFLAGS_DEBUG) $(ARCH_CFLAGS) -std=gnu99 -ffreestanding -mno-sse -mno-sse2 -mno-mmx -fno-pic -fno-stack-protector -fno-builtin -fno-common -fno-tree-loop-distribute-patterns -fcf-protection=none -fno-asynchronous-unwind-tables -Os -ffunction-sections -fdata-sections -Wall -Wextra

# Expanded to compile all the core kernel files visible in your tree
OBJS = $(BUILD)/hal.o \
       $(BUILD)/entry.o \
       $(BUILD)/isr.o \
       $(BUILD)/usermode.o \
       $(BUILD)/desktop.o \
       $(BUILD)/desktop_gfx.o \
       $(BUILD)/desktop_render.o \
       $(BUILD)/desktop_chrome.o \
       $(BUILD)/font5x7.o \
       $(BUILD)/kernel.o \
       $(BUILD)/drivers.o \
       $(BUILD)/keyboard.o \
       $(BUILD)/panic.o \
       $(BUILD)/mouse_cursor.o \
       $(BUILD)/mouse_init.o \
       $(BUILD)/pit_timer.o \
       $(DRV_OBJS) \
       $(BUILD)/axi.o \
       $(BUILD)/fs.o \
       $(BUILD)/sysdispatch.o \
       $(BUILD)/boot.o \
       $(BUILD)/hwinfo.o \
       $(BUILD)/setup.o \
       $(BUILD)/airp.o \
       $(BUILD)/fb.o \
       $(BUILD)/vbe.o \
       $(BUILD)/airgraph.o \
       $(BUILD)/bmp.o \
       $(BUILD)/image.o \
       $(BUILD)/sprunkui.o

all: $(BUILD)/textile.img

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/hal.o: arch/i386/hal.c include/hal.h include/airos_types.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

# --- Bootloader Stages ---
$(BUILD)/stage1.bin: bootloader/stage1.asm | $(BUILD)
	$(AS) -f bin $< -o $@

$(BUILD)/stage2.bin: bootloader/stage2.asm | $(BUILD)
	$(AS) -f bin -D FORCE_BOOT_MODE=$(BOOT_MODE) $< -o $@
	test `stat -c %s $@` -le 2048
	truncate -s 2048 $@

$(BUILD)/stage2-live.bin: bootloader/stage2.asm | $(BUILD)
	$(AS) -f bin -D FORCE_BOOT_MODE=1 $< -o $@
	test `stat -c %s $@` -le 2048
	truncate -s 2048 $@

$(BUILD)/stage2-setup.bin: bootloader/stage2.asm | $(BUILD)
	$(AS) -f bin -D FORCE_BOOT_MODE=2 $< -o $@
	test `stat -c %s $@` -le 2048
	truncate -s 2048 $@

# --- Kernel Core Assembly Objects ---
$(BUILD)/entry.o: kernel/entry.asm | $(BUILD)
	$(AS) $(ARCH_ASFLAGS) $< -o $@

$(BUILD)/isr.o: kernel/isr.asm | $(BUILD)
	$(AS) $(ARCH_ASFLAGS) $< -o $@

$(BUILD)/usermode.o: kernel/usermode.asm | $(BUILD)
	$(AS) $(ARCH_ASFLAGS) $< -o $@

# --- Kernel Core C Objects ---
$(BUILD)/desktop.o: kernel/desktop.c kernel/kernel.h kernel/textile.h include/desktop_gfx.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/desktop_gfx.o: kernel/desktop_gfx.c kernel/kernel.h kernel/textile.h include/fs.h include/desktop_gfx.h include/desktop_render.h include/desktop_chrome.h include/vbe.h include/airgraph.h include/sprunkui.h include/font5x7.h | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

$(BUILD)/desktop_chrome.o: kernel/desktop_chrome.c include/desktop_chrome.h include/font5x7.h include/airgraph.h | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

$(BUILD)/desktop_render.o: kernel/desktop_render.c include/desktop_render.h include/font5x7.h include/airgraph.h | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

$(BUILD)/font5x7.o: kernel/font5x7.c include/font5x7.h | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

$(BUILD)/kernel.o: kernel/kernel.c kernel/kernel.h kernel/textile.h include/desktop_gfx.h include/fb.h include/vbe.h include/airgraph.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/drivers.o: kernel/drivers.c kernel/kernel.h kernel/textile.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/keyboard.o: kernel/keyboard.c kernel/kernel.h kernel/textile.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/panic.o: kernel/panic.c kernel/kernel.h kernel/textile.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/rtl8139.o: kernel/rtl8139.c kernel/kernel.h kernel/textile.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/axi.o: kernel/axi.c include/axi.h include/desktop_gfx.h include/sprunkui.h kernel/kernel.h kernel/textile.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/fs.o: kernel/fs.c include/fs.h include/drv_ata.h include/kheap.h kernel/kernel.h kernel/textile.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/sysdispatch.o: kernel/sysdispatch.c include/axi.h include/fs.h kernel/kernel.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/boot.o: kernel/boot.c include/boot.h | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

$(BUILD)/hwinfo.o: kernel/hwinfo.c include/hwinfo.h include/boot.h include/hw.h | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

$(BUILD)/setup.o: kernel/setup.c include/setup.h include/boot.h include/drv_ata.h kernel/kernel.h | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

$(BUILD)/airp.o: kernel/airp.c include/airp.h kernel/textile.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/fb.o: kernel/fb.c include/fb.h | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

$(BUILD)/vbe.o: kernel/vbe.c include/vbe.h include/fb.h include/hw.h include/airos_types.h | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

$(BUILD)/airgraph.o: kernel/airgraph.c include/airgraph.h include/fb.h | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

$(BUILD)/bmp.o: kernel/bmp.c include/bmp.h | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

$(BUILD)/image.o: kernel/image.c include/image.h include/bmp.h | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

$(BUILD)/sprunkui.o: kernel/sprunkui.c include/sprunkui.h include/airgraph.h | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

# --- Modular Subsystem Drivers ---
$(BUILD)/mouse_cursor.o: kernel/mouse_cursor.c kernel/kernel.h kernel/textile.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/mouse_init.o: kernel/mouse_init.c kernel/kernel.h kernel/textile.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/pit_timer.o: kernel/pit_timer.c kernel/kernel.h kernel/textile.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/drv_pic.o: drivers/drv_pic.c include/drv_pic.h include/hw.h | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

$(BUILD)/d_%.o: drivers/%.c kernel/kernel.h kernel/textile.h include/*.h | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

# --- Linking and Image Creation ---
$(BUILD)/kernel.elf: $(OBJS) kernel/link.ld
	$(LD) $(ARCH_LDFLAGS) -nostdlib --gc-sections -T kernel/link.ld -o $@ $(OBJS)

$(BUILD)/kernel.bin: $(BUILD)/kernel.elf
	$(OBJCOPY) -O binary $< $@
	test `stat -c %s $@` -le 131072
	truncate -s 131072 $@

# --- User Space App Objects ---
$(BUILD)/app.o: userland/app.c kernel/textile.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/rt.o: userland/rt.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/app.elf: $(BUILD)/app.o $(BUILD)/rt.o userland/app.ld
	$(LD) $(ARCH_LDFLAGS) -nostdlib -T userland/app.ld -o $@ $(BUILD)/app.o $(BUILD)/rt.o

$(BUILD)/app.bin: $(BUILD)/app.elf
	$(OBJCOPY) -O binary $< $@
	test `stat -c %s $@` -le 65536
	truncate -s 65536 $@

# --- Explorer and Terminal AIRP staging area (outside filesystem sectors) ---
$(BUILD)/explorer.o: userland/explorer.c include/axi.h include/fs.h kernel/textile.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/explorer.elf: $(BUILD)/explorer.o $(BUILD)/rt.o userland/app.ld
	$(LD) $(ARCH_LDFLAGS) -nostdlib -T userland/app.ld -o $@ $(BUILD)/explorer.o $(BUILD)/rt.o

$(BUILD)/explorer.bin: $(BUILD)/explorer.elf
	$(OBJCOPY) -O binary $< $@
	test `stat -c %s $@` -le 16348

$(BUILD)/explorer.airp: $(BUILD)/explorer.bin tools/airpack.py
	python3 tools/airpack.py $(BUILD)/explorer.bin $@ explorer
	test `stat -c %s $@` -le 16384


# --- Built-in Settings and Notes AIRP applications ---
$(BUILD)/settings.o: userland/settings.c include/axi.h include/fs.h kernel/textile.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/settings.elf: $(BUILD)/settings.o $(BUILD)/rt.o userland/app.ld
	$(LD) $(ARCH_LDFLAGS) -nostdlib -T userland/app.ld -o $@ $(BUILD)/settings.o $(BUILD)/rt.o
$(BUILD)/settings.bin: $(BUILD)/settings.elf
	$(OBJCOPY) -O binary $< $@
	test `stat -c %s $@` -le 16348
$(BUILD)/settings.airp: $(BUILD)/settings.bin tools/airpack.py
	python3 tools/airpack.py $(BUILD)/settings.bin $@ settings
	test `stat -c %s $@` -le 16384

$(BUILD)/notes.o: userland/notes.c include/axi.h include/fs.h kernel/textile.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/notes.elf: $(BUILD)/notes.o $(BUILD)/rt.o userland/app.ld
	$(LD) $(ARCH_LDFLAGS) -nostdlib -T userland/app.ld -o $@ $(BUILD)/notes.o $(BUILD)/rt.o
$(BUILD)/notes.bin: $(BUILD)/notes.elf
	$(OBJCOPY) -O binary $< $@
	test `stat -c %s $@` -le 8156
$(BUILD)/notes.airp: $(BUILD)/notes.bin tools/airpack.py
	python3 tools/airpack.py $(BUILD)/notes.bin $@ notes
	test `stat -c %s $@` -le 8192

# Assembles the disk image using the precise structural sector positioning offsets
$(BUILD)/textile.img: $(BUILD)/stage1.bin $(BUILD)/stage2.bin $(BUILD)/kernel.bin $(BUILD)/app.bin $(BUILD)/explorer.airp $(BUILD)/terminal.airp $(BUILD)/settings.airp $(BUILD)/notes.airp
	dd if=/dev/zero of=$@ bs=512 count=2880 status=none
	dd if=$(BUILD)/stage1.bin of=$@ bs=512 count=1 conv=notrunc status=none
	dd if=$(BUILD)/stage2.bin of=$@ bs=512 seek=1 count=4 conv=notrunc status=none
	dd if=$(BUILD)/kernel.bin of=$@ bs=512 seek=5 count=256 conv=notrunc status=none
	dd if=$(BUILD)/app.bin of=$@ bs=512 seek=261 count=128 conv=notrunc status=none
	dd if=$(BUILD)/explorer.airp of=$@ bs=512 seek=2447 count=32 conv=notrunc status=none
	dd if=$(BUILD)/terminal.airp of=$@ bs=512 seek=2479 count=64 conv=notrunc status=none
	dd if=$(BUILD)/settings.airp of=$@ bs=512 seek=2543 count=32 conv=notrunc status=none
	dd if=$(BUILD)/notes.airp of=$@ bs=512 seek=2575 count=16 conv=notrunc status=none

$(BUILD)/livecd.img: $(BUILD)/stage1.bin $(BUILD)/stage2-live.bin $(BUILD)/kernel.bin $(BUILD)/app.bin $(BUILD)/explorer.airp $(BUILD)/terminal.airp $(BUILD)/settings.airp $(BUILD)/notes.airp
	dd if=/dev/zero of=$@ bs=512 count=2880 status=none
	dd if=$(BUILD)/stage1.bin of=$@ bs=512 count=1 conv=notrunc status=none
	dd if=$(BUILD)/stage2-live.bin of=$@ bs=512 seek=1 count=4 conv=notrunc status=none
	dd if=$(BUILD)/kernel.bin of=$@ bs=512 seek=5 count=256 conv=notrunc status=none
	dd if=$(BUILD)/app.bin of=$@ bs=512 seek=261 count=128 conv=notrunc status=none
	dd if=$(BUILD)/explorer.airp of=$@ bs=512 seek=2447 count=32 conv=notrunc status=none
	dd if=$(BUILD)/terminal.airp of=$@ bs=512 seek=2479 count=64 conv=notrunc status=none
	dd if=$(BUILD)/settings.airp of=$@ bs=512 seek=2543 count=32 conv=notrunc status=none
	dd if=$(BUILD)/notes.airp of=$@ bs=512 seek=2575 count=16 conv=notrunc status=none

$(BUILD)/bootcd.img: $(BUILD)/stage1.bin $(BUILD)/stage2-setup.bin $(BUILD)/kernel.bin $(BUILD)/app.bin $(BUILD)/explorer.airp $(BUILD)/terminal.airp $(BUILD)/settings.airp $(BUILD)/notes.airp
	dd if=/dev/zero of=$@ bs=512 count=2880 status=none
	dd if=$(BUILD)/stage1.bin of=$@ bs=512 count=1 conv=notrunc status=none
	dd if=$(BUILD)/stage2-setup.bin of=$@ bs=512 seek=1 count=4 conv=notrunc status=none
	dd if=$(BUILD)/kernel.bin of=$@ bs=512 seek=5 count=256 conv=notrunc status=none
	dd if=$(BUILD)/app.bin of=$@ bs=512 seek=261 count=128 conv=notrunc status=none
	dd if=$(BUILD)/explorer.airp of=$@ bs=512 seek=2447 count=32 conv=notrunc status=none
	dd if=$(BUILD)/terminal.airp of=$@ bs=512 seek=2479 count=64 conv=notrunc status=none
	dd if=$(BUILD)/settings.airp of=$@ bs=512 seek=2543 count=32 conv=notrunc status=none
	dd if=$(BUILD)/notes.airp of=$@ bs=512 seek=2575 count=16 conv=notrunc status=none

$(BUILD)/livecd.iso: $(BUILD)/livecd.img
	mkdir -p $(BUILD)/livecd-root
	cp $(BUILD)/livecd.img $(BUILD)/livecd-root/flair-livecd.img
	xorriso -as mkisofs -quiet -V FLAIROS_LIVE -b flair-livecd.img -c boot.catalog -o $@ $(BUILD)/livecd-root

$(BUILD)/bootcd.iso: $(BUILD)/bootcd.img
	mkdir -p $(BUILD)/bootcd-root
	cp $(BUILD)/bootcd.img $(BUILD)/bootcd-root/flair-bootcd.img
	xorriso -as mkisofs -quiet -V FLAIROS_SETUP -b flair-bootcd.img -c boot.catalog -o $@ $(BUILD)/bootcd-root

livecd: $(BUILD)/livecd.iso

bootcd: $(BUILD)/bootcd.iso

boot-media: $(BUILD)/textile.img $(BUILD)/livecd.iso $(BUILD)/bootcd.iso

# Isolated diagnostic image: its own output tree and recovery boot mode.
debug-image:
	$(MAKE) BUILD=build-debug DEBUG=1 BOOT_MODE=0 build-debug/textile.img

# --- Window API demo: same image layout, app slot holds userland/win_app.c ---
$(BUILD)/win_app.o: userland/win_app.c include/axi.h kernel/textile.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/win_app.elf: $(BUILD)/win_app.o $(BUILD)/rt.o userland/app.ld
	$(LD) $(ARCH_LDFLAGS) -nostdlib -T userland/app.ld -o $@ $(BUILD)/win_app.o $(BUILD)/rt.o

$(BUILD)/win_app.bin: $(BUILD)/win_app.elf
	$(OBJCOPY) -O binary $< $@
	test `stat -c %s $@` -le 4096
	test `stat -c %s $@` -le 4096

$(BUILD)/win-demo.img: $(BUILD)/stage1.bin $(BUILD)/stage2.bin $(BUILD)/kernel.bin $(BUILD)/win_app.bin
	dd if=/dev/zero of=$@ bs=512 count=2880 status=none
	dd if=$(BUILD)/stage1.bin of=$@ bs=512 count=1 conv=notrunc status=none
	dd if=$(BUILD)/stage2.bin of=$@ bs=512 seek=1 count=4 conv=notrunc status=none
	dd if=$(BUILD)/kernel.bin of=$@ bs=512 seek=5 count=256 conv=notrunc status=none
	dd if=$(BUILD)/win_app.bin of=$@ bs=512 seek=261 count=128 conv=notrunc status=none

$(BUILD)/storage.img: | $(BUILD)
	truncate -s 31457280 $@

storage-image: $(BUILD)/storage.img

run: $(BUILD)/textile.img $(BUILD)/storage.img
	$(QEMU) -fda $(BUILD)/textile.img -hda $(BUILD)/storage.img -serial stdio -no-reboot

win-demo: $(BUILD)/win-demo.img
	$(QEMU) -fda $(BUILD)/win-demo.img -serial stdio -no-reboot

$(BUILD)/airgraph_host_test: tests/airgraph_host_test.c kernel/airgraph.c kernel/sprunkui.c kernel/fb.c include/airgraph.h include/sprunkui.h include/fb.h | $(BUILD)
	$(HOSTCC) -std=gnu99 -Wall -Wextra -Iinclude -o $@ tests/airgraph_host_test.c kernel/airgraph.c kernel/sprunkui.c kernel/fb.c

$(BUILD)/desktop_chrome_host_test: tests/desktop_chrome_host_test.c kernel/desktop_chrome.c kernel/font5x7.c kernel/airgraph.c kernel/fb.c include/desktop_chrome.h include/font5x7.h | $(BUILD)
	$(HOSTCC) -std=gnu99 -Wall -Wextra -Iinclude -o $@ tests/desktop_chrome_host_test.c kernel/desktop_chrome.c kernel/font5x7.c kernel/airgraph.c kernel/fb.c

$(BUILD)/desktop_render_host_test: tests/desktop_render_host_test.c kernel/desktop_render.c kernel/font5x7.c kernel/airgraph.c kernel/fb.c kernel/bmp.c include/desktop_render.h include/font5x7.h include/bmp.h | $(BUILD)
	$(HOSTCC) -std=gnu99 -Wall -Wextra -Iinclude -o $@ tests/desktop_render_host_test.c kernel/desktop_render.c kernel/font5x7.c kernel/airgraph.c kernel/fb.c kernel/bmp.c

$(BUILD)/bmp_host_test: tests/bmp_host_test.c kernel/bmp.c include/bmp.h | $(BUILD)
	$(HOSTCC) -std=gnu99 -Wall -Wextra -Iinclude -o $@ tests/bmp_host_test.c kernel/bmp.c

$(BUILD)/image_host_test: tests/image_host_test.c kernel/image.c kernel/bmp.c include/image.h include/bmp.h | $(BUILD)
	$(HOSTCC) -std=gnu99 -Wall -Wextra -Iinclude -o $@ tests/image_host_test.c kernel/image.c kernel/bmp.c

$(BUILD)/driver_host_test: tests/driver_host_test.c drivers/drv_core.c include/drv.h include/drv_pic.h | $(BUILD)
	$(HOSTCC) -std=gnu99 -Wall -Wextra -Iinclude -o $@ tests/driver_host_test.c drivers/drv_core.c

$(BUILD)/io_host_test: tests/io_host_test.c drivers/io_manager.c include/io.h | $(BUILD)
	$(HOSTCC) -std=gnu99 -Wall -Wextra -Iinclude -o $@ tests/io_host_test.c drivers/io_manager.c

$(BUILD)/fb_host_test: tests/fb_host_test.c kernel/fb.c include/fb.h | $(BUILD)
	$(HOSTCC) -std=gnu99 -Wall -Wextra -Iinclude -o $@ tests/fb_host_test.c kernel/fb.c

$(BUILD)/ata_identify_host_test: tests/ata_identify_host_test.c drivers/ata_identify.c include/ata_identify.h | $(BUILD)
	$(HOSTCC) -std=gnu99 -Wall -Wextra -Iinclude -o $@ tests/ata_identify_host_test.c drivers/ata_identify.c

$(BUILD)/pmm_host_test: tests/pmm_host_test.c drivers/mm_core.c include/mm_core.h | $(BUILD)
	$(HOSTCC) -std=gnu99 -Wall -Wextra -Werror -Iinclude -o $@ tests/pmm_host_test.c drivers/mm_core.c

$(BUILD)/vm_region_host_test: tests/vm_region_host_test.c drivers/vm_region.c include/vm_region.h | $(BUILD)
	$(HOSTCC) -std=gnu99 -Wall -Wextra -Werror -Iinclude -o $@ tests/vm_region_host_test.c drivers/vm_region.c

test: $(BUILD)/textile.img $(BUILD)/livecd.iso $(BUILD)/bootcd.iso $(BUILD)/airgraph_host_test $(BUILD)/driver_host_test $(BUILD)/io_host_test $(BUILD)/fb_host_test $(BUILD)/ata_identify_host_test $(BUILD)/pmm_host_test $(BUILD)/vm_region_host_test $(BUILD)/desktop_render_host_test $(BUILD)/desktop_chrome_host_test $(BUILD)/bmp_host_test $(BUILD)/image_host_test
	$(BUILD)/airgraph_host_test
	$(BUILD)/fb_host_test
	$(BUILD)/driver_host_test
	$(BUILD)/io_host_test
	$(BUILD)/ata_identify_host_test
	$(BUILD)/pmm_host_test
	$(BUILD)/vm_region_host_test
	$(BUILD)/desktop_render_host_test
	$(BUILD)/desktop_chrome_host_test
	$(BUILD)/bmp_host_test
	$(BUILD)/image_host_test
	python3 tests/airp_apps_test.py $(BUILD)/settings.airp $(BUILD)/notes.airp
	python3 tests/smoke.py $(BUILD)/textile.img
	python3 tests/boot_media_test.py $(BUILD)/livecd.iso $(BUILD)/bootcd.iso

clean:
	rm -rf $(BUILD)

.PHONY: all run win-demo test clean livecd bootcd boot-media debug-image storage-image

# --- SDK example app (built on request, not on the disk image) ---
$(BUILD)/hello.o: examples/hello/hello.c sdk/flareos.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/hello.elf: $(BUILD)/hello.o $(BUILD)/rt.o userland/app.ld
	$(LD) $(ARCH_LDFLAGS) -nostdlib -T userland/app.ld -o $@ $(BUILD)/hello.o $(BUILD)/rt.o

$(BUILD)/hello.bin: $(BUILD)/hello.elf
	$(OBJCOPY) -O binary $< $@
	test `stat -c %s $@` -le 65536

$(BUILD)/libc_demo.o: examples/libc_demo/libc_demo.c sdk/flareos.h sdk/flare_libc.h sdk/flare_string.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/libc_demo.elf: $(BUILD)/libc_demo.o $(BUILD)/rt.o userland/app.ld
	$(LD) $(ARCH_LDFLAGS) -nostdlib -T userland/app.ld -o $@ $(BUILD)/libc_demo.o $(BUILD)/rt.o

$(BUILD)/libc_demo.bin: $(BUILD)/libc_demo.elf
	$(OBJCOPY) -O binary $< $@
	test `stat -c %s $@` -le 65536

sdk-example: $(BUILD)/hello.bin $(BUILD)/libc_demo.bin

.PHONY: sdk-example

# --- SDK host checks: native compiler, since the 32-bit fs-test build needs multilib headers ---
HOSTCC ?= cc
sdk-test: | $(BUILD)
	$(HOSTCC) -std=gnu99 -Wall -Wextra -Isdk -o $(BUILD)/sdk_host_test tests/sdk_host_test.c
	$(BUILD)/sdk_host_test

.PHONY: sdk-test

# Compile public C headers as a freestanding 32-bit app (no host libc).
sdk-header-test: | $(BUILD)
	$(CC) -m32 -std=gnu99 -ffreestanding -fno-builtin -Wall -Wextra -Isdk -fsyntax-only tests/sdk_header_test.c

.PHONY: sdk-header-test

# --- Host test for the RAM filesystem (runs on this machine, not in the kernel) ---
fs-test: | $(BUILD)
	$(CC) -m32 -std=gnu99 -Wall -Wextra -Iinclude -o $(BUILD)/fs_host_test tests/fs_host_test.c
	$(BUILD)/fs_host_test

.PHONY: fs-test

# --- Standalone GUI Terminal userland application ---
$(BUILD)/terminal.o: userland/terminal/terminal.c include/axi.h include/fs.h kernel/textile.h sdk/stdlib.h sdk/string.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/terminal.elf: $(BUILD)/terminal.o $(BUILD)/rt.o userland/app.ld
	$(LD) $(ARCH_LDFLAGS) -nostdlib -T userland/app.ld -o $@ $(BUILD)/terminal.o $(BUILD)/rt.o

$(BUILD)/terminal.bin: $(BUILD)/terminal.elf
	$(OBJCOPY) -O binary $< $@
	test `stat -c %s $@` -le 32732

$(BUILD)/terminal.airp: $(BUILD)/terminal.bin tools/airpack.py
	python3 tools/airpack.py $(BUILD)/terminal.bin $@ terminal
	test `stat -c %s $@` -le 32768

terminal-app: $(BUILD)/terminal.airp

.PHONY: terminal-app

# --- Optional MicroPython port (upstream source is fetched, not vendored) ---
MICROPYTHON_TAG ?= v1.29.0
MICROPYTHON_DIR ?= $(BUILD)/third_party/micropython
MICROPYTHON_BUILD ?= $(abspath $(BUILD)/micropython)

micropython-fetch: | $(BUILD)
	python3 tools/fetch_micropython.py "$(MICROPYTHON_DIR)" "$(MICROPYTHON_TAG)"

micropython-port: micropython-fetch
	rm -rf "$(MICROPYTHON_DIR)/ports/flareos"
	mkdir -p "$(MICROPYTHON_DIR)/ports/flareos"
	cp -R ports/micropython/. "$(MICROPYTHON_DIR)/ports/flareos/"
	$(MAKE) -C "$(MICROPYTHON_DIR)/ports/flareos" BUILD="$(MICROPYTHON_BUILD)"

micropython-package: micropython-port
	test `stat -c %s "$(MICROPYTHON_BUILD)/flareos.bin"` -le 131072
	python3 tools/airpack.py "$(MICROPYTHON_BUILD)/flareos.bin" "$(MICROPYTHON_BUILD)/flareos.airp" micropython

.PHONY: micropython-fetch micropython-port micropython-package

$(BUILD)/textile-python.img: $(BUILD)/textile.img micropython-package
	cp $(BUILD)/textile.img $@
	dd if="$(MICROPYTHON_BUILD)/flareos.airp" of=$@ bs=512 seek=2623 count=257 conv=notrunc status=none
	test `stat -c %s $@` -eq 1474560

python-image: $(BUILD)/textile-python.img

.PHONY: python-image
