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
(driver counters), `echo <text>`, and the hardware commands `pci`, `can [n]`,
`net`, `arp <ip>`, `sched rr|fp`, and `rt [overload|reset]` described below. Use it with
`qemu-system-i386 -cdrom os.iso -serial stdio` (or `-serial mon:stdio`).

## Scope: QEMU-emulated hardware only

The PCI, CAN, and Ethernet code below was written against, and only ever run
on, hardware **emulated by QEMU** (8.2.2, TCG, from this repo's Docker image).
It has **never** been run on real hardware: no physical CAN controller or bus,
no physical RTL8139 NIC, no real PCI bus, and no timing measured on a real PIT.
Register behaviour matches what QEMU models, which is not guaranteed to match
silicon. The real-time numbers are measured on QEMU's emulated PIT under
emulation (here also under amd64-on-Arm emulation), so they show that the
counters and policy work, not what a real machine would achieve.

## PCI, CAN, Ethernet, and real-time scheduling

**PCI bus scan** (`pci.c`). Configuration mechanism #1: address port 0xCF8, data
port 0xCFC. Boot enumerates bus 0 (and buses behind PCI-PCI bridges), prints
`PCI: bb:ss.f vendor:device class:subclass` for each function to VGA and the
debug port, and keeps a table drivers use to find their device and BARs.
The `pci` shell command lists it.

**CAN** (`can.c`, `can_frame.c`). A polled driver for the SJA1000 controller on
QEMU's emulated Kvaser PCIcanx card (`kvaser_pci`, PCI 10e8:8406, I/O BAR1),
used in PeliCAN mode with an accept-all filter. Run two cards on one
`can-bus` object:

```sh
qemu-system-i386 -cdrom os.iso -serial stdio \
  -object can-bus,id=canbus0 \
  -device kvaser_pci,canbus=canbus0 -device kvaser_pci,canbus=canbus0
```

`can [n]` sends `n` frames (default 64) alternately controller 0 -> 1 and 1 -> 0,
receives each on the peer, and compares ID, standard/extended flag, DLC, and data.
It reports `sent`, `received`, `mismatches`, and timeouts. Frames mix 11-bit and
29-bit IDs, DLC 0-8, and zero-length remote frames. Known limitation, observed
rather than root-caused: remote frames with DLC > 0 desynchronised QEMU 8.2.2's
receive FIFO in this setup, so the self-test uses DLC 0 for remote frames. The
frame codec itself (`can_frame.c`, with DLC > 0 remote frames) is covered by
native unit tests.

**Ethernet** (`rtl8139.c`, `net_proto.c`, `net.c`). A polled RTL8139 driver (QEMU
`-device rtl8139`; 8 KiB receive ring and four transmit descriptors in kernel
BSS) under a small hardware-independent stack: Ethernet II parsing, ARP
(replies for 10.0.2.15, a 4-entry cache, and `arp <ip>` to send a who-has), and
IPv4 + ICMP echo reply with checksum verification (corrupt packets are
dropped and counted). `net` prints the MAC, IP, counters, and ARP cache.
Because there is no host-visible interface with user-mode networking, the test
uses a socket netdev (`-netdev socket,connect=...`) so a scripted Python peer
can inject and capture whole frames; a stock host `ping` is **not** used.

**Real-time scheduling** (`scheduler.c`, `rt.c`). `sched fp` switches from the
default round-robin to fixed-priority preemption: the highest-priority ready
task runs, re-evaluated every 10 ms tick; the shell and CPU-bound workers share
the lowest level and rotate every 100 ms. `sched rr` switches back. Three
periodic tasks (implicit deadline = period) are always present:

| task | priority | period | job CPU time |
| --- | --- | --- | --- |
| rt0 | 1 | 50 ms | 10 ms |
| rt1 | 2 | 100 ms | 20 ms |
| rt2 | 3 | 200 ms | 30 ms |

CPU use is charged per 10 ms tick to the running task, so execution times are
accurate to one tick. A job still pending at its deadline counts one deadline
miss and is aborted. `rt` prints, per task, jobs released/completed, deadline
misses, worst response in ticks, and release-to-first-run latency in PIT counts
(1.193182 MHz, ~0.838 us each), plus the min/max interval between consecutive
timer interrupts read from the 8254 latch; the spread is the tick jitter.
`rt overload` makes rt2's next job need 250 ms against a 200 ms deadline, so
exactly one miss is counted. Switching policy resets the counters and drops
in-flight jobs. Sample output (fixed priority, QEMU TCG under Docker):

```
rt: policy=fp tick_samples=520 tick_pit_min=9340 tick_pit_max=14636 tick_jitter_pit=5296 tick_jitter_us=4438
rt0: prio=1 period=5 exec=1 released=104 done=104 deadline_miss=0 resp_max_ticks=1 ...
```

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

Four further boots exercise the new drivers (all against QEMU-emulated devices):

* **PCI + CAN**: the boot log must list the i440FX host bridge and two
  10e8:8406 cards; `can 64` and `can 200` must report `sent == received`
  (264 frames) with zero mismatches and zero timeouts.
* **Ethernet (socket netdev)**: a Python peer sends an ARP request and checks
  the guest's reply fields, sends three ICMP echo requests and checks each
  reply's addresses, IPv4 and ICMP checksums, identifier, sequence, and
  payload, confirms a corrupted-checksum request gets no reply, then answers
  the guest's own `arp 10.0.2.2` and checks the ARP cache and counters.
* **Ethernet (user-mode netdev)**: the guest's ARP request is answered by
  QEMU's SLIRP gateway.
* **Real-time**: under round-robin rt0 must miss deadlines; after `sched fp`
  no task may miss, rt0 must respond within two ticks, and workers must still
  progress; `rt overload` must add exactly one miss, to rt2 only.

`make test` additionally runs native unit tests of the ARP/IPv4/ICMP code
(`tests/net_test.c`, including bad checksums, truncation, and wrong
addresses) and of the CAN frame codec (`tests/can_test.c`).

Not verified: anything on real hardware; a real CAN bus or transceiver; a real
NIC or a host `ping` (the ICMP echo requests are scripted frames); interrupt-
driven CAN/NIC operation (both drivers poll); and the absolute timing figures,
which come from an emulated PIT.

`scripts/smoke.py` also writes JUnit XML (`build/junit.xml`, or `$JUNIT_XML`)
with one test case per QEMU boot (nine now), even when a case fails, and exits non-zero on
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
