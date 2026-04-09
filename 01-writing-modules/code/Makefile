# Out-of-tree Makefile for Bootlin Lab kernel modules
# Target: Linux 4.9.88 on NXP i.MX6ULL (ARMv7)

ifneq ($(KERNELRELEASE),)
    # Second invocation: Kbuild picks up obj-m to build .ko
    obj-m := hello_version.o
else
    # First invocation: invoke kernel Makefile with M=$(PWD)
    KDIR := $(HOME)/100ask_imx6ull-sdk/Linux-4.9.88
    PWD := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
endif
