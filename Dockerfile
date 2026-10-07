# Build with --platform linux/amd64 on ARM hosts.
FROM ubuntu:24.04
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    gcc-multilib make nasm grub-pc-bin grub-common xorriso qemu-system-x86 python3 gdb \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /work
CMD ["make", "smoke"]
