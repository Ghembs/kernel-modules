TARGET_MODULE:=simple-module

ifneq ($(KERNELRELEASE),)
	$(TARGET_MODULE)-objs := prova.o device.o
	obj-m := $(TARGET_MODULE).o
else
	#BUILDSYSTEM_DIR:=/lib/modules/$(shell uname -r)/build
	BUILDSYSTEM_DIR:=/home/volumio/Documenti/VOLUMIO/sunxi-linux-5.0
	PWD:=$(shell pwd)
all :
	$(MAKE) ARCH=arm64 CROSS_COMPILE=/opt/toolchain/gcc-linaro-6.3.1-2017.02-x86_64_aarch64-linux-gnu/bin/aarch64-linux-gnu- -C $(BUILDSYSTEM_DIR) M=$(shell pwd) modules
clean:
	$(MAKE) -C $(BUILDSYSTEM_DIR) M=$(shell pwd) clean
load:
	/sbin/insmod ./$(TARGET_MODULE).ko
unload:
	/sbin/rmmod ./$(TARGET_MODULE).ko
endif
