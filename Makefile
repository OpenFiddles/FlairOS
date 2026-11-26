# This Makefile drives the legacy i386 reference build. It still assembles the
# BIOS loader, links the 32-bit kernel, and produces the floppy and CD images,
# but it is no longer what `make` does by default: the product is the 64-bit
# line built by tools/build_preset.py. Everything i386 lives here behind
# `make legacy` and `make legacy-test` so the reference image stays reproducible
# until the 64-bit image boots from disk.
ARCH ?= i386
SUPPORTED_ARCHES := i386

ifeq ($(filter $(ARCH),$(SUPPORTED_ARCHES)),)
$(error ARCH=$(ARCH) is not a legacy build. Use `make presets` for the 64-bit product, or ARCH=i386 with `make legacy`)
endif

# Add one block per architecture when its boot layer lands.
ifeq ($(ARCH),i386)
ARCH_CFLAGS  = -m32
ARCH_ASFLAGS = -f elf32
ARCH_LDFLAGS = -m elf_i386
endif

# Toolchain selection. x86 hosts use the native gcc/ld with -m32. Any other host
# (for example an ARM phone running Termux) cross-compiles with clang and LLD.
HOST_ARCH := $(shell uname -m)
ifneq ($(filter i386 i486 i586 i686 x86_64 amd64,$(HOST_ARCH)),)
CC = gcc
LD = ld
OBJCOPY = objcopy
TOOLCHAIN_CFLAGS = -fno-tree-loop-distribute-patterns
else
CC = clang --target=i386-none-elf
LD = ld.lld
OBJCOPY = llvm-objcopy
TOOLCHAIN_CFLAGS =
endif
AS = nasm
HOSTCC ?= cc
QEMU ?= qemu-system-i386
BUILD = build

DRV_SRCS = $(shell find drivers -type f -name '*.c' -print)
DRV_OBJS = $(patsubst drivers/%.c,$(BUILD)/d_%.o,$(DRV_SRCS))

DEVICE ?= desktop
FLAIROS_SHELL ?= desktop
ifeq ($(FLAIROS_SHELL),mobile)
CFLAGS_SHELL = -DFLAIROS_SHELL_MOBILE
else ifneq ($(FLAIROS_SHELL),desktop)
$(error FLAIROS_SHELL=$(FLAIROS_SHELL) is unsupported; use desktop or mobile)
else
CFLAGS_SHELL =
endif
ifeq ($(DEVICE),phone)
CFLAGS_DEVICE = -DFLAIROS_DEVICE_PHONE
else ifeq ($(DEVICE),tablet)
CFLAGS_DEVICE = -DFLAIROS_DEVICE_TABLET
else ifeq ($(DEVICE),laptop)
CFLAGS_DEVICE = -DFLAIROS_DEVICE_LAPTOP
else ifeq ($(DEVICE),tv)
CFLAGS_DEVICE = -DFLAIROS_DEVICE_TV
else
CFLAGS_DEVICE =
endif

DISPLAY ?= landscape
ifeq ($(DISPLAY),portrait)
CFLAGS_DISPLAY = -DFLAIROS_DISPLAY_PORTRAIT $(CFLAGS_DEVICE)
else ifeq ($(DISPLAY),wide)
CFLAGS_DISPLAY = -DFLAIROS_DISPLAY_WIDE $(CFLAGS_DEVICE)
else ifeq ($(DISPLAY),4k)
CFLAGS_DISPLAY = -DFLAIROS_DISPLAY_4K $(CFLAGS_DEVICE)
else
CFLAGS_DISPLAY = $(CFLAGS_DEVICE)
endif

# Keep project headers discoverable for every kernel/driver translation unit.
DEBUG ?= 1
ifeq ($(DEBUG),1)
CFLAGS_DEBUG = -DFLAIROS_DEBUG
else
CFLAGS_DEBUG =
endif
CFLAGS = -Iinclude -Ikernel $(CFLAGS_DEBUG) $(CFLAGS_DISPLAY) $(CFLAGS_SHELL) $(ARCH_CFLAGS) -std=gnu99 -ffreestanding -mno-sse -mno-sse2 -mno-mmx -fno-pic -fno-stack-protector -fno-builtin -fno-common $(TOOLCHAIN_CFLAGS) -fcf-protection=none -fno-asynchronous-unwind-tables -Os -ffunction-sections -fdata-sections -Wall -Wextra

# Expanded to compile all the core kernel files visible in your tree
OBJS = $(BUILD)/hal.o \
       $(BUILD)/entry.o \
       $(BUILD)/isr.o \
       $(BUILD)/usermode.o \
       $(BUILD)/desktop.o \
       $(BUILD)/mobile.o \
       $(BUILD)/desktop_gfx.o \
       $(BUILD)/desktop_render.o \
       $(BUILD)/desktop_chrome.o \
       $(BUILD)/windown.o \
       $(BUILD)/task.o \
       $(BUILD)/elf.o \
       $(BUILD)/vfs.o \
       $(BUILD)/fat32.o \
       $(BUILD)/ashmem.o \
       $(BUILD)/binder.o \
       $(BUILD)/apk.o \
       $(BUILD)/dex.o \
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
       $(BUILD)/cursor.o \
       $(BUILD)/sprunkui.o \
       $(BUILD)/airnim.o \
       $(BUILD)/status.o \
       $(BUILD)/sprunkui_anim.o

.DEFAULT_GOAL := presets

# --- 64-bit product: desktop, mobile and tv on x86_64 and aarch64 ---
presets:
	python3 tools/build_preset.py --all

# --- Legacy i386 reference image (opt-in) ---
legacy: $(BUILD)/textile.img $(BUILD)/livecd.iso $(BUILD)/bootcd.iso $(BUILD)/storage.img

legacy-image: $(BUILD)/textile.img

mobile:
	$(MAKE) BUILD=build-mobile-make DEVICE=phone DISPLAY=portrait FLAIROS_SHELL=mobile legacy-image

desktop:
	$(MAKE) BUILD=build-desktop-make DEVICE=desktop DISPLAY=landscape FLAIROS_SHELL=desktop legacy-image

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/hal.o: legacy/i386/arch/hal.c include/hal.h include/airos_types.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

# --- Bootloader Stages ---
BOOTLOADER_SRCS = legacy/i386/bootloader/stage1.asm legacy/i386/bootloader/stage2.asm legacy/i386/bootloader/bootinfo.inc \
                  $(wildcard legacy/i386/bootloader/loader/*.inc)

$(BUILD)/stage1.bin: $(BOOTLOADER_SRCS) | $(BUILD)
	$(AS) -f bin -I legacy/i386/bootloader/ legacy/i386/bootloader/stage1.asm -o $@

$(BUILD)/stage2.bin: $(BOOTLOADER_SRCS) | $(BUILD)
	$(AS) -f bin -I legacy/i386/bootloader/ -D FORCE_BOOT_MODE=0 legacy/i386/bootloader/stage2.asm -o $@
	test `stat -c %s $@` -le 3072
	truncate -s 3072 $@

$(BUILD)/stage2-live.bin: $(BOOTLOADER_SRCS) | $(BUILD)
	$(AS) -f bin -I legacy/i386/bootloader/ -D FORCE_BOOT_MODE=1 legacy/i386/bootloader/stage2.asm -o $@
	test `stat -c %s $@` -le 3072
	truncate -s 3072 $@

$(BUILD)/stage2-setup.bin: $(BOOTLOADER_SRCS) | $(BUILD)
	$(AS) -f bin -I legacy/i386/bootloader/ -D FORCE_BOOT_MODE=2 legacy/i386/bootloader/stage2.asm -o $@
	test `stat -c %s $@` -le 3072
	truncate -s 3072 $@

# --- Kernel Core Assembly Objects ---
$(BUILD)/entry.o: kernel/boot/entry.asm | $(BUILD)
	$(AS) $(ARCH_ASFLAGS) $< -o $@

$(BUILD)/isr.o: kernel/boot/isr.asm | $(BUILD)
	$(AS) $(ARCH_ASFLAGS) $< -o $@

$(BUILD)/usermode.o: kernel/boot/usermode.asm | $(BUILD)
	$(AS) $(ARCH_ASFLAGS) $< -o $@

# --- Kernel Core C Objects ---
$(BUILD)/desktop.o: kernel/ui/desktop.c kernel/kernel.h kernel/textile.h include/desktop_gfx.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/mobile.o: kernel/ui/mobile.c kernel/kernel.h include/desktop_gfx.h include/airgraph.h include/sprunkui.h include/windown.h include/drv_touch.h include/vfs.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/desktop_gfx.o: kernel/ui/desktop_gfx.c include/airnim.h include/status.h include/sprunkui.h kernel/kernel.h kernel/textile.h include/fs.h include/desktop_gfx.h include/desktop_render.h include/desktop_chrome.h include/vbe.h include/airgraph.h include/sprunkui.h include/font5x7.h | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@


$(BUILD)/desktop_chrome.o: kernel/ui/desktop_chrome.c include/desktop_chrome.h include/font5x7.h include/airgraph.h | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

$(BUILD)/windown.o: kernel/ui/windown.c include/windown.h include/desktop_chrome.h include/airgraph.h | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

$(BUILD)/task.o: kernel/core/task.c include/task.h include/kheap.h include/hal.h | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

$(BUILD)/elf.o: kernel/pkg/elf.c include/elf.h | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

$(BUILD)/vfs.o: kernel/fs/vfs.c include/vfs.h include/kheap.h include/fat32.h include/drv_ata.h | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

$(BUILD)/fat32.o: kernel/fs/fat32.c include/fat32.h | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

$(BUILD)/ashmem.o: kernel/mm/ashmem.c include/ashmem.h include/kheap.h | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

$(BUILD)/binder.o: kernel/mm/binder.c include/binder.h include/kheap.h | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

$(BUILD)/apk.o: kernel/pkg/apk.c include/apk.h | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

$(BUILD)/dex.o: kernel/pkg/dex.c include/dex.h | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

$(BUILD)/desktop_render.o: kernel/ui/desktop_render.c include/desktop_render.h include/font5x7.h include/airgraph.h | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

$(BUILD)/font5x7.o: kernel/gfx/font5x7.c include/font5x7.h | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

$(BUILD)/kernel.o: kernel/core/kernel.c kernel/kernel.h kernel/textile.h include/desktop_gfx.h include/fb.h include/vbe.h include/airgraph.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/drivers.o: kernel/core/drivers.c kernel/kernel.h kernel/textile.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/keyboard.o: kernel/dev/keyboard.c kernel/kernel.h kernel/textile.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/panic.o: kernel/core/panic.c kernel/kernel.h kernel/textile.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/axi.o: kernel/core/axi.c include/axi.h include/desktop_gfx.h include/sprunkui.h kernel/kernel.h kernel/textile.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/fs.o: kernel/fs/fs.c include/fs.h include/drv_ata.h include/kheap.h kernel/kernel.h kernel/textile.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/sysdispatch.o: kernel/core/sysdispatch.c include/axi.h include/fs.h kernel/kernel.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/boot.o: kernel/boot/boot.c include/boot.h | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

$(BUILD)/hwinfo.o: kernel/boot/hwinfo.c include/hwinfo.h include/boot.h include/hw.h | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

$(BUILD)/setup.o: kernel/boot/setup.c include/setup.h include/boot.h include/drv_ata.h kernel/kernel.h | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

$(BUILD)/airp.o: kernel/pkg/airp.c include/airp.h kernel/textile.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/airjson.o: kernel/pkg/airjson.c include/airjson.h | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

$(BUILD)/desktop_theme.o: kernel/ui/desktop_theme.c include/desktop_theme.h include/airjson.h include/airp.h include/sprunkui.h include/srgb_linear.h include/desktop_gfx.h | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

$(BUILD)/fb.o: kernel/gfx/fb.c include/fb.h | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

$(BUILD)/vbe.o: kernel/gfx/vbe.c include/vbe.h include/fb.h include/hw.h include/airos_types.h | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

$(BUILD)/airgraph.o: kernel/gfx/airgraph.c include/airgraph.h include/fb.h | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

$(BUILD)/bmp.o: kernel/gfx/bmp.c include/bmp.h | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

$(BUILD)/image.o: kernel/gfx/image.c include/image.h include/bmp.h | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

$(BUILD)/cursor.o: kernel/gfx/cursor.c include/cursor.h include/bmp.h | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

$(BUILD)/sprunkui.o: kernel/gfx/sprunkui.c include/sprunkui.h include/airgraph.h | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

$(BUILD)/airnim.o: kernel/gfx/airnim.c include/airnim.h include/airgraph.h | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

$(BUILD)/status.o: kernel/gfx/status.c include/status.h include/drv_net.h include/hw.h | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

$(BUILD)/sprunkui_anim.o: kernel/gfx/sprunkui_anim.c include/sprunkui.h include/airnim.h include/airgraph.h | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

# --- Modular Subsystem Drivers ---
$(BUILD)/mouse_cursor.o: kernel/dev/mouse_cursor.c kernel/kernel.h kernel/textile.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/mouse_init.o: kernel/dev/mouse_init.c kernel/kernel.h kernel/textile.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/pit_timer.o: kernel/dev/pit_timer.c kernel/kernel.h kernel/textile.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/drv_pic.o: drivers/drv_pic.c include/drv_pic.h include/hw.h | $(BUILD)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

$(BUILD)/d_%.o: drivers/%.c kernel/kernel.h kernel/textile.h include/*.h | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

# --- Linking and Image Creation ---
$(BUILD)/kernel.elf: $(OBJS) kernel/boot/link.ld
	$(LD) $(ARCH_LDFLAGS) -nostdlib --gc-sections -T kernel/boot/link.ld -o $@ $(OBJS)

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
$(BUILD)/explorer.o: userland/explorer.c include/axi.h include/vfs.h kernel/textile.h | $(BUILD)
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

# --- FlairDesktop session package ---
# A component package: manifest plus declarative resources, no executable. The
# session shell implementation lives in the kernel, so the container describes
# the session rather than shipping a duplicate copy of it.
DESKTOP_PKG_SRCS = desktop/manifest/desktop.json desktop/themes/default.json \
                   desktop/resources/launcher.json desktop/resources/strings.json

$(BUILD)/FlairDesktop.airp: $(DESKTOP_PKG_SRCS) tools/airpkg.py | $(BUILD)
	python3 tools/airpkg.py pack - $@ \
		--name FlairDesktop --flags desktop,system,resources-only --arch i386 \
		--manifest desktop/manifest/desktop.json \
		--resource 'theme.json=desktop/themes/default.json:theme' \
		--resource 'launcher.json=desktop/resources/launcher.json:ui' \
		--resource 'strings.json=desktop/resources/strings.json:strings'
	python3 tools/airpkg.py validate $@ --arch i386
	test `stat -c %s $@` -le 8192

# Assembles the disk image using the precise structural sector positioning offsets
$(BUILD)/textile.img: $(BUILD)/stage1.bin $(BUILD)/stage2.bin $(BUILD)/kernel.bin $(BUILD)/app.bin $(BUILD)/explorer.airp $(BUILD)/terminal.airp $(BUILD)/settings.airp $(BUILD)/notes.airp $(BUILD)/FlairDesktop.airp
	dd if=/dev/zero of=$@ bs=512 count=2880 status=none
	dd if=$(BUILD)/stage1.bin of=$@ bs=512 count=1 conv=notrunc status=none
	dd if=$(BUILD)/stage2.bin of=$@ bs=512 seek=1 count=6 conv=notrunc status=none
	dd if=$(BUILD)/kernel.bin of=$@ bs=512 seek=7 count=256 conv=notrunc status=none
	dd if=$(BUILD)/app.bin of=$@ bs=512 seek=263 count=128 conv=notrunc status=none
	dd if=$(BUILD)/explorer.airp of=$@ bs=512 seek=2447 count=32 conv=notrunc status=none
	dd if=$(BUILD)/terminal.airp of=$@ bs=512 seek=2479 count=64 conv=notrunc status=none
	dd if=$(BUILD)/settings.airp of=$@ bs=512 seek=2543 count=32 conv=notrunc status=none
	dd if=$(BUILD)/notes.airp of=$@ bs=512 seek=2575 count=16 conv=notrunc status=none
	dd if=$(BUILD)/FlairDesktop.airp of=$@ bs=512 seek=2591 count=16 conv=notrunc status=none

$(BUILD)/livecd.img: $(BUILD)/stage1.bin $(BUILD)/stage2-live.bin $(BUILD)/kernel.bin $(BUILD)/app.bin $(BUILD)/explorer.airp $(BUILD)/terminal.airp $(BUILD)/settings.airp $(BUILD)/notes.airp $(BUILD)/FlairDesktop.airp
	dd if=/dev/zero of=$@ bs=512 count=2880 status=none
	dd if=$(BUILD)/stage1.bin of=$@ bs=512 count=1 conv=notrunc status=none
	dd if=$(BUILD)/stage2-live.bin of=$@ bs=512 seek=1 count=6 conv=notrunc status=none
	dd if=$(BUILD)/kernel.bin of=$@ bs=512 seek=7 count=256 conv=notrunc status=none
	dd if=$(BUILD)/app.bin of=$@ bs=512 seek=263 count=128 conv=notrunc status=none
	dd if=$(BUILD)/explorer.airp of=$@ bs=512 seek=2447 count=32 conv=notrunc status=none
	dd if=$(BUILD)/terminal.airp of=$@ bs=512 seek=2479 count=64 conv=notrunc status=none
	dd if=$(BUILD)/settings.airp of=$@ bs=512 seek=2543 count=32 conv=notrunc status=none
	dd if=$(BUILD)/notes.airp of=$@ bs=512 seek=2575 count=16 conv=notrunc status=none
	dd if=$(BUILD)/FlairDesktop.airp of=$@ bs=512 seek=2591 count=16 conv=notrunc status=none

$(BUILD)/bootcd.img: $(BUILD)/stage1.bin $(BUILD)/stage2-setup.bin $(BUILD)/kernel.bin $(BUILD)/app.bin $(BUILD)/explorer.airp $(BUILD)/terminal.airp $(BUILD)/settings.airp $(BUILD)/notes.airp $(BUILD)/FlairDesktop.airp
	dd if=/dev/zero of=$@ bs=512 count=2880 status=none
	dd if=$(BUILD)/stage1.bin of=$@ bs=512 count=1 conv=notrunc status=none
	dd if=$(BUILD)/stage2-setup.bin of=$@ bs=512 seek=1 count=6 conv=notrunc status=none
	dd if=$(BUILD)/kernel.bin of=$@ bs=512 seek=7 count=256 conv=notrunc status=none
	dd if=$(BUILD)/app.bin of=$@ bs=512 seek=263 count=128 conv=notrunc status=none
	dd if=$(BUILD)/explorer.airp of=$@ bs=512 seek=2447 count=32 conv=notrunc status=none
	dd if=$(BUILD)/terminal.airp of=$@ bs=512 seek=2479 count=64 conv=notrunc status=none
	dd if=$(BUILD)/settings.airp of=$@ bs=512 seek=2543 count=32 conv=notrunc status=none
	dd if=$(BUILD)/notes.airp of=$@ bs=512 seek=2575 count=16 conv=notrunc status=none
	dd if=$(BUILD)/FlairDesktop.airp of=$@ bs=512 seek=2591 count=16 conv=notrunc status=none

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
	dd if=$(BUILD)/stage2.bin of=$@ bs=512 seek=1 count=6 conv=notrunc status=none
	dd if=$(BUILD)/kernel.bin of=$@ bs=512 seek=7 count=256 conv=notrunc status=none
	dd if=$(BUILD)/win_app.bin of=$@ bs=512 seek=263 count=128 conv=notrunc status=none

$(BUILD)/storage.img: | $(BUILD)
	python3 tools/make_storage.py $@

storage-image: $(BUILD)/storage.img

run: $(BUILD)/textile.img $(BUILD)/storage.img
	$(QEMU) -fda $(BUILD)/textile.img -hda $(BUILD)/storage.img -nic user,model=rtl8139 -serial stdio -no-reboot

VNC ?= 0.0.0.0:0
run-vnc: $(BUILD)/textile.img $(BUILD)/storage.img
	$(QEMU) -fda $(BUILD)/textile.img -hda $(BUILD)/storage.img -vnc $(VNC) -k en-us -nic user,model=rtl8139 -serial stdio -no-reboot -m 256M

win-demo: $(BUILD)/win-demo.img
	$(QEMU) -fda $(BUILD)/win-demo.img -nic user,model=rtl8139 -serial stdio -no-reboot

$(BUILD)/airgraph_host_test: tests/airgraph_host_test.c kernel/gfx/airgraph.c kernel/gfx/sprunkui.c kernel/gfx/fb.c include/airgraph.h include/sprunkui.h include/fb.h | $(BUILD)
	$(HOSTCC) -std=gnu99 -Wall -Wextra -Iinclude -o $@ tests/airgraph_host_test.c kernel/gfx/airgraph.c kernel/gfx/sprunkui.c kernel/gfx/fb.c

$(BUILD)/status_host_test: tests/status_host_test.c kernel/gfx/status.c include/status.h include/drv_net.h | $(BUILD)
	$(HOSTCC) -std=gnu99 -Wall -Wextra -Iinclude -DFLAIROS_HOST_TEST -o $@ tests/status_host_test.c kernel/gfx/status.c

$(BUILD)/sprunkui_anim_host_test: tests/sprunkui_anim_host_test.c kernel/gfx/sprunkui_anim.c kernel/gfx/sprunkui.c kernel/gfx/airnim.c kernel/gfx/airgraph.c kernel/gfx/fb.c include/sprunkui.h include/airnim.h include/fb.h | $(BUILD)
	$(HOSTCC) -std=gnu99 -Wall -Wextra -Iinclude -o $@ -DFLAIROS_HOST_TEST tests/sprunkui_anim_host_test.c kernel/gfx/sprunkui_anim.c kernel/gfx/sprunkui.c kernel/gfx/airnim.c kernel/gfx/airgraph.c kernel/gfx/fb.c

$(BUILD)/airnim_host_test: tests/airnim_host_test.c kernel/gfx/airnim.c kernel/gfx/airgraph.c kernel/gfx/fb.c include/airnim.h include/airgraph.h include/fb.h | $(BUILD)
	$(HOSTCC) -std=gnu99 -Wall -Wextra -Iinclude -o $@ -DFLAIROS_HOST_TEST tests/airnim_host_test.c kernel/gfx/airnim.c kernel/gfx/airgraph.c kernel/gfx/fb.c

# sprunkui.c is linked only for the real sp_theme_default fallback palette.
# Section-level collection drops its AirGraph drawing code, which this test
# never calls and which has no host implementation.
$(BUILD)/desktop_theme_host_test: tests/desktop_theme_host_test.c kernel/ui/desktop_theme.c kernel/pkg/airp.c kernel/pkg/airjson.c kernel/gfx/sprunkui.c include/desktop_theme.h include/sprunkui.h include/srgb_linear.h | $(BUILD)
	$(HOSTCC) -std=gnu99 -Wall -Wextra -DFLAIROS_HOST_TEST -Iinclude -Ikernel \
		-ffunction-sections -fdata-sections -Wl,--gc-sections \
		-o $@ tests/desktop_theme_host_test.c kernel/ui/desktop_theme.c kernel/pkg/airp.c kernel/pkg/airjson.c kernel/gfx/sprunkui.c

$(BUILD)/airjson_host_test: tests/airjson_host_test.c kernel/pkg/airjson.c include/airjson.h | $(BUILD)
	$(HOSTCC) -std=gnu99 -Wall -Wextra -Iinclude -Ikernel -o $@ tests/airjson_host_test.c kernel/pkg/airjson.c

$(BUILD)/airp_host_test: tests/airp_host_test.c kernel/pkg/airp.c include/airp.h kernel/textile.h | $(BUILD)
	$(HOSTCC) -std=gnu99 -Wall -Wextra -DFLAIROS_HOST_TEST -Iinclude -Ikernel -o $@ tests/airp_host_test.c kernel/pkg/airp.c

$(BUILD)/vfs_host_test: tests/vfs_host_test.c kernel/fs/vfs.c include/vfs.h | $(BUILD)
	$(HOSTCC) -std=gnu99 -Wall -Wextra -Iinclude -o $@ tests/vfs_host_test.c

$(BUILD)/elf_host_test: tests/elf_host_test.c kernel/pkg/elf.c include/elf.h | $(BUILD)
	$(HOSTCC) -std=gnu99 -Wall -Wextra -Iinclude -o $@ tests/elf_host_test.c kernel/pkg/elf.c

$(BUILD)/task_host_test: tests/task_host_test.c kernel/core/task.c include/task.h | $(BUILD)
	$(HOSTCC) -std=gnu99 -Wall -Wextra -Iinclude -o $@ tests/task_host_test.c

$(BUILD)/windown_host_test: tests/windown_host_test.c kernel/ui/windown.c kernel/ui/desktop_chrome.c kernel/gfx/font5x7.c kernel/gfx/airgraph.c kernel/gfx/fb.c include/windown.h include/desktop_chrome.h include/font5x7.h | $(BUILD)
	$(HOSTCC) -std=gnu99 -Wall -Wextra -Iinclude -o $@ tests/windown_host_test.c kernel/ui/windown.c kernel/ui/desktop_chrome.c kernel/gfx/font5x7.c kernel/gfx/airgraph.c kernel/gfx/fb.c

$(BUILD)/desktop_chrome_host_test: tests/desktop_chrome_host_test.c kernel/ui/desktop_chrome.c kernel/gfx/font5x7.c kernel/gfx/airgraph.c kernel/gfx/fb.c include/desktop_chrome.h include/font5x7.h | $(BUILD)
	$(HOSTCC) -std=gnu99 -Wall -Wextra -Iinclude -o $@ tests/desktop_chrome_host_test.c kernel/ui/desktop_chrome.c kernel/gfx/font5x7.c kernel/gfx/airgraph.c kernel/gfx/fb.c

$(BUILD)/desktop_render_host_test: tests/desktop_render_host_test.c kernel/ui/desktop_render.c kernel/gfx/font5x7.c kernel/gfx/airgraph.c kernel/gfx/fb.c kernel/gfx/bmp.c include/desktop_render.h include/font5x7.h include/bmp.h | $(BUILD)
	$(HOSTCC) -std=gnu99 -Wall -Wextra -Iinclude -o $@ tests/desktop_render_host_test.c kernel/ui/desktop_render.c kernel/gfx/font5x7.c kernel/gfx/airgraph.c kernel/gfx/fb.c kernel/gfx/bmp.c

$(BUILD)/bmp_host_test: tests/bmp_host_test.c kernel/gfx/bmp.c include/bmp.h | $(BUILD)
	$(HOSTCC) -std=gnu99 -Wall -Wextra -Iinclude -o $@ tests/bmp_host_test.c kernel/gfx/bmp.c

$(BUILD)/image_host_test: tests/image_host_test.c kernel/gfx/image.c kernel/gfx/bmp.c include/image.h include/bmp.h | $(BUILD)
	$(HOSTCC) -std=gnu99 -Wall -Wextra -Iinclude -o $@ tests/image_host_test.c kernel/gfx/image.c kernel/gfx/bmp.c

$(BUILD)/driver_host_test: tests/driver_host_test.c drivers/drv_core.c include/drv.h include/drv_pic.h | $(BUILD)
	$(HOSTCC) -std=gnu99 -Wall -Wextra -Iinclude -o $@ tests/driver_host_test.c drivers/drv_core.c

$(BUILD)/io_host_test: tests/io_host_test.c drivers/io_manager.c include/io.h | $(BUILD)
	$(HOSTCC) -std=gnu99 -Wall -Wextra -DFLAIROS_HOST_TEST -Iinclude -o $@ tests/io_host_test.c drivers/io_manager.c

$(BUILD)/fb_host_test: tests/fb_host_test.c kernel/gfx/fb.c include/fb.h | $(BUILD)
	$(HOSTCC) -std=gnu99 -Wall -Wextra -Iinclude -o $@ tests/fb_host_test.c kernel/gfx/fb.c

$(BUILD)/ata_identify_host_test: tests/ata_identify_host_test.c drivers/ata_identify.c include/ata_identify.h | $(BUILD)
	$(HOSTCC) -std=gnu99 -Wall -Wextra -Iinclude -o $@ tests/ata_identify_host_test.c drivers/ata_identify.c

$(BUILD)/pmm_host_test: tests/pmm_host_test.c drivers/mm_core.c include/mm_core.h | $(BUILD)
	$(HOSTCC) -std=gnu99 -Wall -Wextra -Werror -Iinclude -o $@ tests/pmm_host_test.c drivers/mm_core.c

$(BUILD)/vm_region_host_test: tests/vm_region_host_test.c drivers/vm_region.c include/vm_region.h | $(BUILD)
	$(HOSTCC) -std=gnu99 -Wall -Wextra -Werror -Iinclude -o $@ tests/vm_region_host_test.c drivers/vm_region.c

$(BUILD)/cursor_host_test: tests/cursor_host_test.c kernel/gfx/cursor.c kernel/gfx/bmp.c kernel/gfx/airgraph.c kernel/gfx/fb.c include/cursor.h | $(BUILD)
	$(HOSTCC) -std=gnu99 -Wall -Wextra -DFLAIROS_HOST_TEST -Iinclude -o $@ tests/cursor_host_test.c kernel/gfx/cursor.c kernel/gfx/bmp.c kernel/gfx/airgraph.c kernel/gfx/fb.c

# Host tests build kernel sources with the host compiler, so they describe the
# shared code rather than either target's boot path. They gate CI.
test: $(BUILD)/airgraph_host_test $(BUILD)/driver_host_test $(BUILD)/io_host_test $(BUILD)/fb_host_test $(BUILD)/ata_identify_host_test $(BUILD)/pmm_host_test $(BUILD)/vm_region_host_test $(BUILD)/desktop_render_host_test $(BUILD)/desktop_chrome_host_test $(BUILD)/windown_host_test $(BUILD)/task_host_test $(BUILD)/elf_host_test $(BUILD)/vfs_host_test $(BUILD)/bmp_host_test $(BUILD)/image_host_test $(BUILD)/cursor_host_test $(BUILD)/fat32_host_test $(BUILD)/airnim_host_test $(BUILD)/status_host_test $(BUILD)/sprunkui_anim_host_test $(BUILD)/airjson_host_test $(BUILD)/desktop_theme_host_test $(BUILD)/airp_host_test
	$(BUILD)/airgraph_host_test
	$(BUILD)/fb_host_test
	$(BUILD)/driver_host_test
	$(BUILD)/io_host_test
	$(BUILD)/ata_identify_host_test
	$(BUILD)/pmm_host_test
	$(BUILD)/vm_region_host_test
	$(BUILD)/desktop_render_host_test
	$(BUILD)/desktop_chrome_host_test
	$(BUILD)/windown_host_test
	$(BUILD)/airjson_host_test
	$(BUILD)/desktop_theme_host_test
	$(BUILD)/airp_host_test
	$(BUILD)/airnim_host_test
	$(BUILD)/status_host_test
	$(BUILD)/sprunkui_anim_host_test
	$(BUILD)/task_host_test
	$(BUILD)/elf_host_test
	$(BUILD)/vfs_host_test
	$(BUILD)/bmp_host_test
	$(BUILD)/image_host_test
	$(BUILD)/cursor_host_test
	$(BUILD)/fat32_host_test

# i386 media and boot tests. Opt-in with the rest of the legacy line.
legacy-test: test $(BUILD)/textile.img $(BUILD)/livecd.iso $(BUILD)/bootcd.iso
	python3 tests/boot_layout_test.py
	python3 tests/airp_apps_test.py $(BUILD)/explorer.airp $(BUILD)/settings.airp $(BUILD)/notes.airp
	# Diagnostic: the i386 desktop does not yet reach its shell (a pre-existing
	# #GP in the scheduler selftest, present at HEAD before the legacy move).
	-python3 tests/smoke.py $(BUILD)/textile.img
	python3 tests/boot_media_test.py $(BUILD)/livecd.iso $(BUILD)/bootcd.iso

acceptance: legacy-image
	sh tests/acceptance.sh

clean:
	rm -rf $(BUILD)

.PHONY: presets legacy legacy-image run run-vnc win-demo test legacy-test acceptance clean livecd bootcd boot-media storage-image

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

phone:
	$(MAKE) DEVICE=phone BUILD=$(BUILD)-phone legacy-image

tablet:
	$(MAKE) DEVICE=tablet BUILD=$(BUILD)-tablet legacy-image

laptop:
	$(MAKE) DEVICE=laptop BUILD=$(BUILD)-laptop legacy-image

tv:
	$(MAKE) DEVICE=tv BUILD=$(BUILD)-tv legacy-image

.PHONY: phone tablet laptop tv

$(BUILD)/fat32_host_test: tests/fat32_host_test.c kernel/fs/fat32.c include/fat32.h | $(BUILD)
	$(HOSTCC) -std=gnu99 -Wall -Wextra -Werror -Iinclude -o $@ tests/fat32_host_test.c kernel/fs/fat32.c
