# FlareOS i386 port for MicroPython v1.29.0.
# Copied into the pinned upstream tree at ports/flareos.
include ../../py/mkenv.mk

CROSS = 0
QSTR_DEFS = qstrdefsport.h
MICROPY_ROM_TEXT_COMPRESSION ?= 0
include $(TOP)/py/py.mk

CC = gcc
LD = gcc
OBJCOPY = objcopy

INC += -I.
INC += -I$(TOP)
INC += -I$(BUILD)

CFLAGS += $(INC) -m32 -std=gnu99 -Os -ffreestanding -fno-pic \
	-fno-stack-protector -fno-builtin -fno-common \
	-fno-asynchronous-unwind-tables -fcf-protection=none \
	-fno-tree-loop-distribute-patterns -Wall -Wextra

LDFLAGS += -m32 -nostdlib -Wl,-T,app.ld -Wl,--build-id=none
LIBS += $(shell $(CC) -m32 -print-libgcc-file-name)

SRC_C = \
	main.c \
	mphalport.c \
	appheap.c \
	modflareos.c \
	shared/libc/printf.c \
	shared/libc/string0.c \
	shared/readline/readline.c \
	shared/runtime/pyexec.c \
	shared/runtime/stdout_helpers.c

SRC_QSTR += modflareos.c
SRC_QSTR += shared/readline/readline.c shared/runtime/pyexec.c

OBJ += $(PY_CORE_O)
OBJ += $(addprefix $(BUILD)/, $(SRC_C:.c=.o))

all: $(BUILD)/flareos.elf $(BUILD)/flareos.bin

$(BUILD)/flareos.elf: $(OBJ) app.ld
	$(ECHO) "LINK $@"
	$(Q)$(LD) $(LDFLAGS) -o $@ $(OBJ) $(LIBS)
	$(Q)$(SIZE) $@

$(BUILD)/flareos.bin: $(BUILD)/flareos.elf
	$(Q)$(OBJCOPY) -O binary $< $@
	@echo "FlareOS MicroPython image size: $$(stat -c %s $@) bytes (AIRP v1 limit: 65536 bytes)"

include $(TOP)/py/mkrules.mk

.PHONY: all
