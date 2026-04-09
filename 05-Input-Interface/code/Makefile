KERN_DIR ?= $(HOME)/100ask_imx6ull-sdk/Linux-4.9.88
obj-m += invensense_mpu6500_input.o
all:
	$(MAKE) -C $(KERN_DIR) M=$(PWD) ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- modules
clean:
	$(MAKE) -C $(KERN_DIR) M=$(PWD) clean
