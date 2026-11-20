ROOT := $(abspath ..)
BUILD := $(ROOT)/build/NullKernel
HOST_OBJ := $(BUILD)/obj/host

BOARD ?= lm3s6965evb
ifeq ($(BOARD),pico_w)
BOARD_NAME := rp2040
else
BOARD_NAME := $(BOARD)
endif

CROSS ?= arm-none-eabi-
CC := $(CROSS)gcc
AS := $(CROSS)gcc
LD := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
HOSTCC ?= cc

# Legacy freestanding RP2040 backend. The supported Pico W build is the Pico
# SDK + TinyUSB CMake project in nullk/pico/.
ifeq ($(BOARD_NAME),rp2040)
ARCH_DIR := arch/arm/dev/rp2040
ARCH_FLAGS := -mcpu=cortex-m0plus
BOARD_BUILD := $(BUILD)/rp2040
CROSS_OBJ := $(BOARD_BUILD)/obj
ELF := $(BOARD_BUILD)/null_kernel_rp2040.elf
BIN := $(BOARD_BUILD)/null_kernel_rp2040.bin
UF2 := $(BOARD_BUILD)/null_kernel_rp2040.uf2
BOOT2_OBJ := $(BOARD_BUILD)/boot2.o
BOOT2_ELF := $(BOARD_BUILD)/boot2.elf
BOOT2_RAW := $(BOARD_BUILD)/boot2_raw.bin
BOOT2 := $(BOARD_BUILD)/boot2.bin
FLASH_BIN := $(BOARD_BUILD)/null_kernel_flash.bin
EXTRA_CFLAGS := -DNULLK_ARCH_RP2040
EXTRA_C_SRC := $(ARCH_DIR)/usb.c
else
ARCH_DIR := arch/arm/cortexm
ARCH_FLAGS := -mcpu=cortex-m3
BOARD_BUILD := $(BUILD)
CROSS_OBJ := $(BUILD)/obj/cross
ELF := $(BUILD)/null_kernel.elf
BIN := $(BUILD)/null_kernel.bin
EXTRA_CFLAGS :=
EXTRA_C_SRC :=
endif

ARCH_CFLAGS := $(ARCH_FLAGS) -mthumb -ffreestanding -fno-builtin \
               -fno-stack-protector -nostdlib -Wall -Wextra -Werror -O2
CFLAGS := -std=c11 $(ARCH_CFLAGS) $(EXTRA_CFLAGS) -MMD -MP -Iinclude -Ink
ASFLAGS := $(ARCH_CFLAGS)
LDFLAGS := -std=c11 $(ARCH_CFLAGS) -T $(ARCH_DIR)/linker.ld \
           -Wl,-Map=$(ELF:.elf=.map)

HOST_CFLAGS := -std=c11 -Wall -Wextra -Werror -O2 -MMD -MP -Iinclude -Ink \
               -DNK_ARCH_HOST

ARCH_C_SRC := $(ARCH_DIR)/arch.c
KERNEL_C_SRC := nk/_oskernel.c nk/_os_runmode.c nk/_os_task.c \
                nk/_os_sched.c nk/_os_capabilities.c nk/_os_memory.c \
                nk/_os_ipc.c nk/_os_irq.c nk/_os_syscalls.c \
                nk/hdf/HardwareDriverEntry.c nk/_os_compiler_rt.c \
                lib/libnk/libnk.c apps/init/app.c $(ARCH_C_SRC) $(EXTRA_C_SRC)
KERNEL_ASM_SRC := $(ARCH_DIR)/boot.s $(ARCH_DIR)/context.s
CROSS_OBJ_FILES := $(patsubst %.c,$(CROSS_OBJ)/%.o,$(KERNEL_C_SRC)) \
                   $(patsubst %.s,$(CROSS_OBJ)/%.o,$(KERNEL_ASM_SRC))

HOST_TEST_SRC := $(filter-out lib/libnk/libnk.c $(ARCH_C_SRC) $(EXTRA_C_SRC),$(KERNEL_C_SRC)) \
                 tests/host_arch.c tests/test_nullk.c
HOST_OBJ_FILES := $(patsubst %.c,$(HOST_OBJ)/%.o,$(HOST_TEST_SRC)) \
                  $(HOST_OBJ)/lib/libnk/libnk.o

.PHONY: all test qemu flash clean pico-sdk

ifeq ($(BOARD_NAME),rp2040)
all: $(UF2)
else
all: $(ELF) $(BIN)
endif

$(CROSS_OBJ)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(CROSS_OBJ)/%.o: %.s
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) -c $< -o $@

$(ELF): $(CROSS_OBJ_FILES) $(ARCH_DIR)/linker.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(CROSS_OBJ_FILES) -lgcc

$(BIN): $(ELF)
	$(OBJCOPY) -O binary $< $@

$(BOOT2_OBJ): $(ARCH_DIR)/boot2.S
	@mkdir -p $(dir $@)
	$(CC) $(ARCH_CFLAGS) -c $< -o $@

$(BOOT2_ELF): $(BOOT2_OBJ) $(ARCH_DIR)/boot2.ld
	@mkdir -p $(dir $@)
	$(LD) $(ARCH_CFLAGS) -nostdlib -Wl,-T,$(ARCH_DIR)/boot2.ld \
		-o $@ $<

$(BOOT2_RAW): $(BOOT2_ELF)
	$(OBJCOPY) -O binary $< $@

$(BOOT2): $(BOOT2_RAW) tools/rp2040_boot2.py
	python3 tools/rp2040_boot2.py $< $@

$(FLASH_BIN): $(BOOT2) $(BIN)
	cat $^ > $@

$(UF2): $(FLASH_BIN) tools/uf2.py
	python3 tools/uf2.py $< $@

$(HOST_OBJ)/%.o: %.c
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOST_CFLAGS) -c $< -o $@

$(HOST_OBJ)/lib/libnk/libnk.o: lib/libnk/libnk.c
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOST_CFLAGS) -c $< -o $@

$(HOST_OBJ)/test_nullk: $(HOST_OBJ_FILES)
	$(HOSTCC) $(HOST_CFLAGS) -o $@ $(HOST_OBJ_FILES)

test: $(HOST_OBJ)/test_nullk
	$(HOST_OBJ)/test_nullk

ifeq ($(BOARD_NAME),rp2040)
flash: $(UF2)
	picotool load -u -x $<

qemu:
	@echo "QEMU does not emulate RP2040; use 'make BOARD=rp2040 flash' instead"
else
qemu: $(ELF)
	qemu-system-arm -M lm3s6965evb -nographic -display none \
		-kernel $(ELF)

flash:
	@echo "'flash' is only available for BOARD=rp2040/pico_w"
endif

# Supported Pico W build: Pico SDK + TinyUSB (see nullk/pico/README).
pico-sdk:
	cmake -S pico -B $(BUILD)/pico -DPICO_BOARD=pico_w
	cmake --build $(BUILD)/pico -j4

clean:
	rm -rf $(BUILD)

-include $(CROSS_OBJ_FILES:.o=.d)
-include $(HOST_OBJ_FILES:.o=.d)
