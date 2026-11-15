CROSS ?= aarch64-linux-gnu-
CC = $(CROSS)gcc
LD = $(CROSS)ld
CFLAGS = -std=gnu99 -ffreestanding -fno-pic -fno-stack-protector -fno-builtin -mgeneral-regs-only -nostdlib -Wall -Wextra -O2 -mcpu=cortex-a72

all: kernel.elf

boot.o: start.S
	$(CC) $(CFLAGS) -c start.S -o boot.o

kernel.o: kernel.c
	$(CC) $(CFLAGS) -c kernel.c -o kernel.o

kernel.elf: boot.o kernel.o linker.ld
	$(LD) -T linker.ld -o kernel.elf boot.o kernel.o

hello: hello.elf
hello.o: hello.c
	$(CC) $(CFLAGS) -c hello.c -o hello.o
hello.elf: boot.o hello.o linker.ld
	$(LD) -T linker.ld -o hello.elf boot.o hello.o

clean:
	rm -f *.o kernel.elf hello.elf

.PHONY: all clean hello
