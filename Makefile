CC = gcc
LD = ld
AS = nasm
CFLAGS = -m32 -std=gnu11 -ffreestanding -fno-builtin -fno-pie -fno-stack-protector -mno-sse -mno-mmx -msoft-float -Og -g -Wall -Wextra -Werror
LDFLAGS = -m elf_i386 -T linker.ld
OBJS = build/boot.o build/kernel.o build/vga.o build/interrupts.o build/paging.o build/isr.o build/keyboard.o
all: os.iso
build:
	mkdir -p build
build/%.o: %.c kprint.h io.h paging.h Makefile | build
	$(CC) $(CFLAGS) -c $< -o $@
build/%.o: %.s Makefile | build
	$(AS) -f elf32 -g -F dwarf $< -o $@
build/kernel.elf: $(OBJS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)
	grub-file --is-x86-multiboot $@
os.iso: build/kernel.elf grub.cfg
	mkdir -p iso/boot/grub
	cp build/kernel.elf iso/boot/kernel.elf
	cp grub.cfg iso/boot/grub/grub.cfg
	grub-mkrescue -o $@ iso
run: os.iso
	qemu-system-i386 -cdrom os.iso -debugcon stdio -no-reboot -no-shutdown
smoke: os.iso
	python3 scripts/smoke.py
debug: os.iso
	qemu-system-i386 -cdrom os.iso -debugcon stdio -S -s -no-reboot -no-shutdown
clean:
	rm -rf build iso os.iso
.PHONY: all run smoke debug clean
