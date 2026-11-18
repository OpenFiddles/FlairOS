ARCH ?= i386
SUPPORTED_ARCHES := i386

ifeq ($(filter $(ARCH),$(SUPPORTED_ARCHES)),)
$(error ARCH=$(ARCH) has no complete platform/boot layer yet. AArch64 HAL groundwork exists, but ARM64 is not a supported boot target)
endif

CC = gcc
AS = nasm
LD = ld
OBJCOPY = objcopy
QEMU ?= qemu-system-i386
BUILD = build

ifeq ($(ARCH),i386)
ARCH_CFLAGS  = -m32
ARCH_ASFLAGS = -f elf32
ARCH_LDFLAGS = -m elf_i386
HAL_SRC = arch/i386/hal.c
endif

DRV_SRCS = $(shell find drivers -type f -name '*.c' -print)
DRV_OBJS = $(patsubst drivers/%.c,$(BUILD)/d_%.o,$(DRV_SRCS))
DISPLAY ?= landscape
ifeq ($(DISPLAY),portrait)
CFLAGS_DISPLAY = -DFLAIROS_DISPLAY_PORTRAIT
else ifeq ($(DISPLAY),wide)
CFLAGS_DISPLAY = -DFLAIROS_DISPLAY_WIDE
else
CFLAGS_DISPLAY =
endif
CFLAGS = -Iinclude -Ikernel $(CFLAGS_DISPLAY) $(ARCH_CFLAGS) -std=gnu99 -ffreestanding -mno-sse -mno-sse2 -mno-mmx -fno-pic -fno-stack-protector -fno-builtin -fno-common -fno-tree-loop-distribute-patterns -fcf-protection=none -fno-asynchronous-unwind-tables -Os -ffunction-sections -fdata-sections -Wall -Wextra

OBJS = $(BUILD)/hal.o $(BUILD)/entry.o $(BUILD)/isr.o $(BUILD)/usermode.o \
       $(BUILD)/desktop.o $(BUILD)/desktop_gfx.o $(BUILD)/desktop_render.o \
       $(BUILD)/font5x7.o $(BUILD)/kernel.o $(BUILD)/drivers.o \
       $(BUILD)/keyboard.o $(BUILD)/panic.o $(BUILD)/mouse_cursor.o \
       $(BUILD)/mouse_init.o $(BUILD)/pit_timer.o $(DRV_OBJS) \
       $(BUILD)/axi.o $(BUILD)/fs.o $(BUILD)/sysdispatch.o $(BUILD)/boot.o \
       $(BUILD)/hwinfo.o $(BUILD)/setup.o $(BUILD)/airp.o $(BUILD)/fb.o \
       $(BUILD)/vbe.o $(BUILD)/airgraph.o $(BUILD)/bmp.o $(BUILD)/sprunkui.o

all: $(BUILD)/textile.img
$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/hal.o: $(HAL_SRC) include/hal.h include/airos_types.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

