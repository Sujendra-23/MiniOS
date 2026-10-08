# MiniOS

A small 32-bit x86 kernel in freestanding C and NASM, booted by GRUB Multiboot.
It installs its own flat GDT, handles CPU exceptions 0–31, remaps the 8259 PIC,
and enables only PIT timer (IRQ0), PS/2 keyboard (IRQ1), and COM1 serial (IRQ4) interrupts.
It enables 32-bit paging with 4 KiB supervisor pages: addresses below 4 MiB
are identity-mapped, except page zero. Kernel code and constants are read-only;
CR0 write protection enforces this in ring 0. A linker check keeps the kernel,
its boot stack, and static page tables inside the mapped window.

A bitmap frame allocator reads GRUB's Multiboot memory map, reserves firmware,
kernel, and boot-module memory, and manages available RAM up to 128 MiB. Two-level
paging can map and unmap additional supervisor pages; empty dynamic page tables
are released. Page-table frames come from the low identity window so the kernel
can access them directly.

PIT IRQ0 drives a round-robin preemptive scheduler with a 100 ms quantum.
The boot context is task 0; three CPU-bound ring-0 workers occupy tasks 1–3.
Each worker has an 8 KiB stack backed by allocated frames and an unmapped guard
page. The assembly interrupt wrapper restores the interrupt frame selected by
the scheduler. Workers never yield or sleep, so progress in all three requires
timer preemption. A private stack marker checks that their contexts stay intact.
The bottom VGA row shows ticks, the current task, and context switches.

The PIT runs at approximately 100 Hz. A tick counter stays on the bottom VGA
row while the text console scrolls above it. Basic unshifted US keyboard input,
Enter, Tab, and Backspace are supported. F12 deliberately raises an invalid
opcode exception; F11 deliberately raises general protection. Both print the
vector, error code, and instruction address, then halt. F10 reads the first
unmapped address (4 MiB), F9 writes to protected kernel code, and F8 reads page
zero. Page faults also report CR2 and decode the access type and fault cause.
Restart QEMU after any deliberate fault.

## Serial console

An interrupt-driven 16550 UART driver runs COM1 at 115200 8N1. Boot detects the
UART with a loopback self-test and checks for its 16-byte FIFOs. IRQ4 moves
received bytes into a 256-byte ring buffer and drains a 256-byte transmit ring
through the THR-empty interrupt, which is enabled only while output is queued.
The driver counts IRQs, bytes, dropped bytes, and line overruns.

Task 0 runs a small command shell: between interrupts it consumes the RX ring,
echoes input, and handles Backspace. Commands: `help`, `uptime`, `sched`
(current task, switches, worker counters), `mem` (free frames), `uart`
(driver counters), and `echo <text>`. Use it with
`qemu-system-i386 -cdrom os.iso -serial stdio` (or `-serial mon:stdio`).

## Build and run on Linux

On Ubuntu 24.04:

```sh
sudo apt-get update
sudo apt-get install -y gcc-multilib make nasm grub-pc-bin grub-common xorriso qemu-system-x86 python3 gdb
make
make run
```

Outputs: `build/kernel.elf` (with debug symbols) and `os.iso`.
The ISO explicitly uses GRUB's i386-pc (BIOS) modules so hosts with extra EFI
packages produce the same boot image. Override GRUB_DIR if these modules are
installed elsewhere. Close the QEMU window to exit. `make clean` removes generated artifacts.

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

`make test` runs the native C frame-allocator unit tests: reservations, partial
page alignment, duplicate availability ranges, exhaustion, unique allocation,
free/reuse, double-free rejection, address limits, and every bitmap word.

`make smoke` runs those unit tests, then boots the actual GRUB ISO under QEMU
TCG, waits for 300 timer ticks, drives the serial shell over a host socket,
injects PS/2 keys through QMP, reads VGA memory to confirm keyboard echo
and the timer display, and verifies the kernel halts cleanly on exceptions.
Five boots verify paging is enabled with write protection, exercise both
exception stack layouts (invalid opcode and general protection), and check
three real page faults: an unmapped read, a null read, and a protected-code
write. Tests confirm vector 14, error codes 0/3, CR2, and VGA diagnostics while
also verifying timer and keyboard IRQs work with paging enabled. It checks that
all three CPU-bound worker counters increase across scheduler snapshots and that
context switches keep occurring. Boot also performs a dynamic page-map/write/
read/unmap self-test and checks the allocator's free-frame count is restored.
The serial check runs every shell command, sends a 206-byte line in one
write (far beyond the 16-byte FIFO) and expects it echoed intact, tests
Backspace editing, and requires the driver's received-byte counter to equal the
bytes the host sent, with zero drops and overruns.
QEMU debug port 0xE9 mirrors console output and emits timer progress.

`scripts/smoke.py` also writes JUnit XML (`build/junit.xml`, or `$JUNIT_XML`)
with one test case per QEMU boot, even when a case fails, and exits non-zero on
any failure. `make report` renders it to `build/smoke-report.html` with
`scripts/junit-to-html.xsl` (needs `xsltproc`). CI uploads both as the
`smoke-report` artifact and adds a pass count to the job summary. The native
frame-allocator tests (`make test`) print to the log but are not in the XML.
The GitHub Actions workflow uses the same Docker toolchain as the macOS
instructions, runs the build and tests, and uploads the ISO and ELF as artifacts.

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
print current
print switches
print tasks[1].work
```

At function entry, the commands above read the interrupt frame from the C
argument on the stack, even when GDB reports the named argument unavailable.
Use `x/16hx 0xb8000` to inspect VGA memory. Docker users can run QEMU and GDB inside the same tools
container.

## Scope

This is a single-core ring-0 teaching kernel. Tasks share one address space;
there are no user-mode processes, per-process page directories, demand paging,
filesystem, or full keyboard driver. Physical-frame allocation is capped at
128 MiB, and page-table frames must fit in the low 4 MiB identity window.
Task creation and memory-management calls run with interrupts disabled. A task
that returns stops running; its stack remains reserved until reboot. Page faults
print diagnostics and halt rather than allocating pages or resuming the code.

Paging follows the two-level 32-bit paging and page-fault rules in the
[Intel system programming manual](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/64-ia-32-architectures-software-developer-system-programming-manual-325384.pdf).
