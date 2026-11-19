obj-m += nullkern_lkm.o

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

# Extra kbuild variables, e.g. KERNEL_ARGS="ARCH=um SUBARCH=arm64 LLVM=1".
KERNEL_ARGS ?=

ccflags-y += -I$(src)/include -I$(src)/../../SDK/include \
	-I$(src)/../../kernel/core/include -I$(src)/../../kernel/linux/include
CC ?= cc
PROBE ?= nullkern_ioctl_probe

.PHONY: all um-arm64 clean install uninstall extension probe

all:
	$(MAKE) -C $(KDIR) M=$(PWD) $(KERNEL_ARGS) modules

probe:
	$(CC) -std=c11 -Wall -Wextra -I$(PWD)/include \
		$(PWD)/tools/nullkern_ioctl_probe.c -o $(PWD)/$(PROBE)

# Convenience target for the um-arm64 tree (https://github.com/zalexdev/linux-um-arm64).
# Usage: make KDIR=/path/to/linux-um-arm64 um-arm64
# Add LLVM=1 if that tree was built with clang instead of gcc.
um-arm64:
	$(MAKE) -C $(KDIR) M=$(PWD) ARCH=um SUBARCH=arm64 $(if $(LLVM),LLVM=$(LLVM),) modules

extension:
	python setup.py build_ext --inplace

install: all
	sudo insmod nullkern_lkm.ko

uninstall:
	sudo rmmod nullkern_lkm || true

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) $(KERNEL_ARGS) clean
	rm -rf build *.egg-info gothicos_linux_port*.so linux_port.c $(PROBE)
