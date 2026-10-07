# MiniOS

A small 32-bit x86 kernel in freestanding C and NASM, booted by GRUB Multiboot.
It installs its own flat GDT, handles CPU exceptions 0–31, remaps the 8259 PIC,
and enables only PIT timer (IRQ0) and PS/2 keyboard (IRQ1) interrupts.
It enables 32-bit paging with 4 KiB supervisor pages: addresses below 4 MiB
are identity-mapped, except page zero. Kernel code and constants are read-only;
CR0 write protection enforces this in ring 0. A linker check keeps the kernel,
its stack, and page tables inside the mapped window.

The PIT runs at approximately 100 Hz. A tick counter stays on the bottom VGA
row while the text console scrolls above it. Basic unshifted US keyboard input,
Enter, Tab, and Backspace are supported. F12 deliberately raises an invalid
opcode exception; F11 deliberately raises general protection. Both print the
vector, error code, and instruction address, then halt. F10 reads the first
unmapped address (4 MiB), F9 writes to protected kernel code, and F8 reads page
zero. Page faults also report CR2 and decode the access type and fault cause.
Restart QEMU after any deliberate fault.

## Build and run on Linux

On Ubuntu 24.04:

```sh
sudo apt-get update
sudo apt-get install -y gcc-multilib make nasm grub-pc-bin grub-common xorriso qemu-system-x86 python3 gdb
make
make run
```

Outputs: `build/kernel.elf` (with debug symbols) and `os.iso`.
Close the QEMU window to exit. `make clean` removes generated artifacts.

## Build and test on macOS with Docker

Start Docker Desktop, then run from this directory:

```sh
docker build --platform linux/amd64 -t minios-tools .
docker run --rm --platform linux/amd64 -v "$PWD:/work" minios-tools
```

This builds the ISO and runs the headless smoke test. For an interactive window,
install QEMU on the host (`brew install qemu`), then run:

```sh
qemu-system-i386 -cdrom os.iso -debugcon stdio -no-reboot -no-shutdown
```

## Verification

`make smoke` boots the actual GRUB ISO under QEMU TCG, waits for 300 timer
ticks, injects PS/2 keys through QMP, reads VGA memory to confirm keyboard echo
and the timer display, and verifies the kernel halts cleanly on exceptions.
Five boots verify paging is enabled with write protection, exercise both
exception stack layouts (invalid opcode and general protection), and check
three real page faults: an unmapped read, a null read, and a protected-code
write. Tests confirm vector 14, error codes 0/3, CR2, and VGA diagnostics while
also verifying timer and keyboard IRQs work with paging enabled.
QEMU debug port 0xE9 mirrors console output and emits timer progress.
The GitHub Actions workflow runs the same build and test and uploads the ISO
and ELF as artifacts.

## Debug with GDB

In one terminal, run `make debug`. In another:

```sh
gdb build/kernel.elf
```

Then:

```gdb
set architecture i386
target remote :1234
break kernel_main
continue
break *interrupt_dispatch
continue
set $irq_frame = *(struct interrupt_frame **)($esp + 4)
print *$irq_frame
```

At function entry, the commands above read the interrupt frame from the C
argument on the stack, even when GDB reports the named argument unavailable.
Use `x/16hx 0xb8000` to
inspect VGA memory. Docker users can run QEMU and GDB inside the same tools
container.

## Scope

This is a single-core ring-0 teaching kernel with a fixed identity map.
Memory above 4 MiB, dynamic allocation, demand paging, user processes, a
filesystem, and a full keyboard driver are not implemented. Page faults print
diagnostics and halt; they do not allocate pages or resume the faulting code.

Paging follows the two-level 32-bit paging and page-fault rules in the
[Intel system programming manual](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/64-ia-32-architectures-software-developer-system-programming-manual-325384.pdf).
