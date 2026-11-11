CROSS ?=
CC = $(CROSS)gcc
LD = $(CROSS)ld
CFLAGS = -std=gnu99 -ffreestanding -fno-pic -fno-stack-protector -fno-builtin -mgeneral-regs-only -O2 -Wall -Wextra

all: hello.elf

boot.o: boot.S
	$(CC) -c boot.S -o boot.o

hello.o: hello.c
	$(CC) $(CFLAGS) -c hello.c -o hello.o

hello.elf: boot.o hello.o linker.ld
	$(LD) -T linker.ld -o hello.elf boot.o hello.o

clean:
	rm -f *.o hello.elf

.PHONY: all clean
