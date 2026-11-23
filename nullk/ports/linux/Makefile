# Native null_kernel Linux port.
#
# `make` builds only the out-of-tree userspace side; no Linux source is needed.
# `make module KDIR=/path/to/linux-6.6.162` builds the LKM when a tree exists.
#
# Phased plan:
#   1. this LKM + ABI v3 shim + PID 1 replacement (implemented here)
#   2. optional Linux patches under patches/optional (not applied by make)

PORT_ROOT := $(abspath .)
NULLK_ROOT := $(abspath ../..)
BUILD := $(abspath ../../../build/NullKernel/linux)
OBJ := $(BUILD)/obj

CC ?= gcc
CXX ?= g++
KDIR ?= /lib/modules/$(shell uname -r)/build

INCLUDES := -Iinclude \
            -I$(NULLK_ROOT)/include \
            -I$(NULLK_ROOT)/apps/include \
            -I$(NULLK_ROOT)/securekernel/include \
            -I/usr/include/drm

COMMON_DEFS := -D_start=nk64_amd64_init_start \
               -DNULLK_LIBNK64_EXTERNAL_SYSCALL

USER_CFLAGS := -std=c11 -Wall -Wextra -Werror -O2 -g $(COMMON_DEFS) $(INCLUDES)
USER_CXXFLAGS := -std=c++17 -Wall -Wextra -Werror -O2 -g \
                 -fno-exceptions -fno-rtti $(COMMON_DEFS) $(INCLUDES)
LINK_FLAGS := -static -pthread -no-pie \
              -Wl,-Ttext-segment=0x10000000

PORT_C_SRC := src/user/init_pid1.c src/user/nk64_syscall_linux.c \
              src/user/fb_console.c
PORT_C_OBJ := $(patsubst src/%.c,$(OBJ)/port/%.o,$(PORT_C_SRC))

NULLK_C_SRC := apps/init64/main.c \
               apps/interfacekit/main.c \
               apps/interfacekit_demo/main.c \
               apps/sh64/main.c \
               apps/usb64/main.c \
               apps/libnk64.c \
               shared/shell64.c \
               shared/sandbox.c \
               shared/usb_as_sata.c \
               shared/printk.c \
               shared/interfacekit_gfx.c \
               shared/interfacekit_widgets.c
NULLK_C_OBJ := $(patsubst $(NULLK_ROOT)/%.c,$(OBJ)/%.o,$(addprefix $(NULLK_ROOT)/,$(NULLK_C_SRC)))

NULLK_CXX_SRC := securekernel/OSSecureAPI.cpp \
                 securekernel/src/api.cpp \
                 securekernel/src/sha256.cpp \
                 securekernel/src/engine.cpp \
                 securekernel/src/policy.cpp
NULLK_CXX_OBJ := $(patsubst $(NULLK_ROOT)/%.cpp,$(OBJ)/%.o,$(addprefix $(NULLK_ROOT)/,$(NULLK_CXX_SRC)))

USER_ELF := $(BUILD)/nullk-linux.elf
INIT_SRC := src/user/initramfs_init.c
INIT_OBJ := $(OBJ)/initramfs/initramfs_init.o
INIT_BIN := $(BUILD)/initramfs-init
TEST_SRC := tests/test_abi_layout.c
TEST_OBJ := $(OBJ)/tests/test_abi_layout.o
KEYMAP_TEST_SRC := tests/test_evdev_keycode.c
KEYMAP_TEST_OBJ := $(OBJ)/tests/test_evdev_keycode.o
KEYMAP_TEST_BIN := $(BUILD)/test_evdev_keycode
TEST_BIN := $(BUILD)/test_linux_port

.PHONY: all user module test initramfs clean module-clean

all: user test

user: $(USER_ELF) $(INIT_BIN)

$(OBJ)/port/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(OBJ)/%.o: $(NULLK_ROOT)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(OBJ)/%.o: $(NULLK_ROOT)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(USER_CXXFLAGS) -c $< -o $@

$(USER_ELF): $(PORT_C_OBJ) $(NULLK_C_OBJ) $(NULLK_CXX_OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(LINK_FLAGS) -o $@ $^

$(INIT_OBJ): $(INIT_SRC)
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(INIT_BIN): $(INIT_OBJ)
	@mkdir -p $(dir $@)
	$(CC) -static -no-pie -o $@ $^

$(TEST_OBJ): $(TEST_SRC)
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(TEST_BIN): $(TEST_OBJ) $(OBJ)/port/user/nk64_syscall_linux.o \
              $(OBJ)/apps/libnk64.o
	@mkdir -p $(dir $@)
	$(CC) -pthread -o $@ $^

$(KEYMAP_TEST_OBJ): $(KEYMAP_TEST_SRC)
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(KEYMAP_TEST_BIN): $(KEYMAP_TEST_OBJ)
	@mkdir -p $(dir $@)
	$(CC) -o $@ $^

test: $(TEST_BIN) $(KEYMAP_TEST_BIN)
	$(TEST_BIN)
	$(KEYMAP_TEST_BIN)

module:
	$(MAKE) -C $(KDIR) M=$(PORT_ROOT) modules
	@mkdir -p $(BUILD)
	cp -f $(PORT_ROOT)/nkabi3.ko $(BUILD)/nkabi3.ko

initramfs: user
	python3 scripts/mkinitramfs.py --out $(BUILD)/initrd-6.6.162-nk.img \
		--init $(INIT_BIN) --elf $(USER_ELF) --module $(BUILD)/nkabi3.ko

module-clean:
	-$(MAKE) -C $(KDIR) M=$(PORT_ROOT) clean

clean: module-clean
	rm -rf $(BUILD)
	rm -f $(PORT_ROOT)/nkabi3.ko $(PORT_ROOT)/Module.symvers \
	      $(PORT_ROOT)/modules.order
