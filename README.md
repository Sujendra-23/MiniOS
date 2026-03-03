# MiniOS

MiniOS is a minimal 32-bit hobby operating system kernel built to demonstrate core operating system concepts including:

- Multiboot booting via GRUB
- Protected mode entry
- Interrupt Descriptor Table (IDT) setup
- PIC remapping
- Keyboard interrupt handling (IRQ1)
- VGA text-mode output
- Freestanding C + Assembly kernel development

This project is designed for learning low-level systems programming and OS fundamentals.

---

# Project Structure


mini-os/
│
├── Makefile
├── linker.ld
├── grub.cfg
│
├── boot.s
├── isr.s
│
├── kernel.c
├── keyboard.c
├── vga.c
├── kprint.h
│
├── build/ (generated)
├── iso/ (generated)
└── os.iso (generated)


---

# Requirements

You must build this project inside a Linux environment.

### Required Packages (Ubuntu/Debian)


build-essential
gcc-multilib
nasm
binutils
grub-pc-bin
xorriso
qemu-system-i386


---

# Running on Linux (Native)

## 1. Install Dependencies

```bash
sudo dpkg --add-architecture i386
sudo apt update
sudo apt install -y build-essential gcc-multilib nasm binutils grub-pc-bin xorriso qemu-system-i386
2. Build the Kernel
make clean
make

This generates:

build/kernel.elf

os.iso

3. Run in QEMU
qemu-system-i386 -cdrom os.iso -serial stdio

To run with VGA window:

qemu-system-i386 -cdrom os.iso
Running on macOS (Recommended: Multipass VM)

Since macOS does not support -m32 builds natively, use Multipass to create a lightweight Ubuntu VM.

1. Install Multipass
brew install --cask multipass
2. Launch Ubuntu VM
multipass launch --name minios --mem 4G --disk 10G 22.04
3. Transfer Project into VM
multipass transfer -r mini-os minios:/home/ubuntu/mini-os
4. Enter VM and Install Dependencies
multipass shell minios
sudo dpkg --add-architecture i386
sudo apt update
sudo apt install -y build-essential gcc-multilib nasm binutils grub-pc-bin xorriso qemu-system-i386
5. Build Inside VM
cd ~/mini-os
make
6. Run in QEMU
qemu-system-i386 -cdrom os.iso -serial stdio
Debugging with GDB

Start QEMU in debug mode:

qemu-system-i386 -cdrom os.iso -s -S

In another terminal:

gdb build/kernel.elf
(gdb) target remote :1234
(gdb) break kernel_main
(gdb) continue
Troubleshooting
-m32 errors

Install gcc-multilib and add i386 architecture:

sudo dpkg --add-architecture i386
sudo apt update
grub-mkrescue not found

Install:

sudo apt install grub-pc-bin xorriso
Black screen in QEMU

Use:

qemu-system-i386 -cdrom os.iso -serial stdio
Future Improvements

Implement PIT timer interrupt

Add preemptive scheduler

Implement paging (virtual memory)

Add syscall interface

Implement simple shell

License

This project is for educational purposes.


---

Now:

```bash
git add README.md
git commit -m "Add README with build and run instructions"
git push
