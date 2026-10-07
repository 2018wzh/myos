TOOLPREFIX ?= riscv64-linux-gnu-

CC      := $(TOOLPREFIX)gcc
LD      := $(TOOLPREFIX)ld
OBJCOPY := $(TOOLPREFIX)objcopy
OBJDUMP := $(TOOLPREFIX)objdump

QEMU ?= qemu-system-riscv64
XXD ?= xxd

CFLAGS := -std=gnu11 -Wall -Wextra -Werror -O2 -g -ffreestanding -fno-common \
	-fno-builtin -fno-omit-frame-pointer -mcmodel=medany -march=rv64gc \
	-mabi=lp64d -mno-relax -msmall-data-limit=0 -fno-pie -fno-pic \
	-fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables \
	-nostdlib -nostartfiles -I src/kernel
LDFLAGS := -z max-page-size=4096 --no-relax
