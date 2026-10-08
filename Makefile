CC = gcc
HOSTCC ?= cc
LD = ld
AS = nasm
GRUB_DIR ?= /usr/lib/grub/i386-pc
CFLAGS = -m32 -std=gnu11 -ffreestanding -fno-builtin -fno-pie -fno-stack-protector -mno-sse -mno-mmx -msoft-float -Og -g -Wall -Wextra -Werror
LDFLAGS = -m elf_i386 -T linker.ld
OBJS = build/boot.o build/kernel.o build/vga.o build/interrupts.o build/paging.o build/frame.o build/memory.o build/scheduler.o build/isr.o build/keyboard.o build/uart.o build/shell.o
all: os.iso
build:
	mkdir -p build
build/%.o: %.c $(wildcard *.h) Makefile | build
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
	grub-mkrescue -d $(GRUB_DIR) -o $@ iso
run: os.iso
	qemu-system-i386 -cdrom os.iso -debugcon stdio -no-reboot -no-shutdown
build/frame-test: tests/frame_test.c frame.c frame.h Makefile | build
	$(HOSTCC) -std=c11 -O2 -Wall -Wextra -Werror -I. frame.c tests/frame_test.c -o $@
test: build/frame-test
	./build/frame-test
smoke: test os.iso
	python3 scripts/smoke.py
report:
	xsltproc -o build/smoke-report.html scripts/junit-to-html.xsl build/junit.xml
	@echo "wrote build/smoke-report.html"
debug: os.iso
	qemu-system-i386 -cdrom os.iso -debugcon stdio -S -s -no-reboot -no-shutdown
clean:
	rm -rf build iso os.iso
.PHONY: all run test smoke report debug clean
