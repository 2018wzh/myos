include common.mk

.DEFAULT_GOAL := all
.DELETE_ON_ERROR:

HARTS ?= 2
TIMER_INTERVAL ?= 1000000

ifneq ($(HARTS),1)
ifneq ($(HARTS),2)
$(error HARTS must be 1 or 2)
endif
endif

CFLAGS += -DHART_COUNT=$(HARTS) -DTIMER_INTERVAL=$(TIMER_INTERVAL)
BUILD_DIR := target/harts$(HARTS)-timer$(TIMER_INTERVAL)
CFLAGS += -I$(BUILD_DIR)

C_SRCS := $(wildcard src/kernel/*.c) \
	$(wildcard src/kernel/boot/*.c) \
	$(wildcard src/kernel/lib/*.c) \
	$(wildcard src/kernel/lock/*.c) \
	$(wildcard src/kernel/mem/*.c) \
	$(wildcard src/kernel/arch/*.c) \
	$(wildcard src/kernel/proc/*.c) \
	$(wildcard src/kernel/trap/*.c)
S_SRCS := $(wildcard src/kernel/boot/*.S) \
	$(wildcard src/kernel/arch/*.S) \
	$(wildcard src/kernel/proc/*.S) \
	$(wildcard src/kernel/trap/*.S)
OBJS := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(C_SRCS)) \
	$(patsubst src/%.S,$(BUILD_DIR)/%.o,$(S_SRCS))
USER_OBJ := $(BUILD_DIR)/user/initcode.o
USER_ELF := $(BUILD_DIR)/user/initcode.elf
USER_BIN := $(BUILD_DIR)/user/initcode.bin
INITCODE_H := $(BUILD_DIR)/initcode.h
DEPS := $(OBJS:.o=.d) $(USER_OBJ:.o=.d)
CONFIG_ELF := $(BUILD_DIR)/kernel-qemu.elf

-include $(DEPS)

.PHONY: all run clean FORCE

all: kernel-qemu.elf

kernel-qemu.elf: FORCE $(CONFIG_ELF)
	cp $(CONFIG_ELF) $@

$(CONFIG_ELF): $(OBJS) kernel.ld Makefile common.mk
	$(LD) $(LDFLAGS) -T kernel.ld -o $@ $(OBJS)

$(USER_ELF): $(USER_OBJ) user.ld Makefile common.mk
	$(LD) $(LDFLAGS) -T user.ld -o $@ $(USER_OBJ)

$(USER_BIN): $(USER_ELF)
	$(OBJCOPY) -O binary $< $@

$(INITCODE_H): $(USER_BIN)
	$(XXD) -i -n initcode $< > $@.tmp
	mv $@.tmp $@

$(BUILD_DIR)/kernel/proc/proc.o: $(INITCODE_H)

FORCE:

$(BUILD_DIR)/%.o: src/%.c Makefile common.mk
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

$(BUILD_DIR)/%.o: src/%.S Makefile common.mk
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

run: kernel-qemu.elf
	$(QEMU) -machine virt -bios none -kernel $< -m 128M -smp $(HARTS) \
		-nographic -serial mon:stdio

clean:
	rm -rf target kernel-qemu.elf
