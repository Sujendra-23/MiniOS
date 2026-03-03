cat > Makefile <<'EOF'
# Simple Makefile for MiniOS
CC = gcc
LD = ld
AS = nasm
CFLAGS = -m32 -nostdinc -fno-builtin -fno-stack-protector -ffreestanding -O2 -Wall -Wextra
LDFLAGS = -m elf_i386 -T linker.ld --oformat elf32-i386

OBJS = kernel.o vga.o isr.o keyboard.o boot.o

all: os.iso

build/kernel.elf: $(OBJS)
	mkdir -p build
	$(LD) $(LDFLAGS) -o build/kernel.elf $(OBJS)

kernel.o: kernel.c kprint.h
	$(CC) $(CFLAGS) -c kernel.c -o kernel.o

vga.o: vga.c kprint.h
	$(CC) $(CFLAGS) -c vga.c -o vga.o

keyboard.o: keyboard.c kprint.h
	$(CC) $(CFLAGS) -c keyboard.c -o keyboard.o

boot.o: boot.s
	nasm -f elf32 boot.s -o boot.o

isr.o: isr.s kprint.h
	nasm -f elf32 isr.s -o isr.o

os.iso: build/kernel.elf grub.cfg
	mkdir -p iso/boot/grub
	cp build/kernel.elf iso/boot/kernel.elf
	cp grub.cfg iso/boot/grub/grub.cfg
	grub-mkrescue -o os.iso iso

clean:
	rm -rf *.o build iso os.iso

.PHONY: all clean
EOF