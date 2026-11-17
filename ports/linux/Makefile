obj-m += nullkern_lkm.o

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

ccflags-y += -I$(src)/include

.PHONY: all clean install uninstall extension

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

extension:
	python setup.py build_ext --inplace

install: all
	sudo insmod nullkern_lkm.ko

uninstall:
	sudo rmmod nullkern_lkm || true

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -rf build *.egg-info gothicos_linux_port*.so linux_port.c
