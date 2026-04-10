# Out-of-tree Makefile for i.MX6ULL kernel modules
# Target: Linux 4.9.88

KERN_DIR ?= $(HOME)/100ask_imx6ull-sdk/Linux-4.9.88

obj-m += custom_uart.o

# ARMv7 architecture + suppress init_module attribute warnings (WSL2 GCC 10+ vs kernel 4.9)
EXTRA_CFLAGS += -march=armv7-a -Wno-missing-attributes

all:
	$(MAKE) -C $(KERN_DIR) M=$(PWD) ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- modules

clean:
	$(MAKE) -C $(KERN_DIR) M=$(PWD) clean
