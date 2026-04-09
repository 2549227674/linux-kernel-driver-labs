# Out-of-tree Makefile for i.MX6ULL kernel modules
# Target: Linux 4.9.88

KERN_DIR ?= $(HOME)/100ask_imx6ull-sdk/Linux-4.9.88

obj-m += custom_uart_dma.o

all:
	$(MAKE) -C $(KERN_DIR) M=$(PWD) ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- modules

clean:
	$(MAKE) -C $(KERN_DIR) M=$(PWD) clean
