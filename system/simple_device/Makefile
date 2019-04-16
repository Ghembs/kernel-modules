TARGET_MODULE:=simple-module

ifneq ($(KERNELRELEASE),)
	$(TARGET_MODULE)-objs := prova.o device.o
	obj-m := $(TARGET_MODULE).o
else
	BUILDSYSTEM_DIR:=/lib/modules/$(shell uname -r)/build
	PWD:=$(shell pwd)
all :
	$(MAKE) -C $(BUILDSYSTEM_DIR) M=$(shell pwd) modules
clean:
	$(MAKE) -C $(BUILDSYSTEM_DIR) M=$(shell pwd) clean
load:
	/sbin/insmod ./$(TARGET_MODULE).ko
unload:
	/sbin/rmmod ./$(TARGET_MODULE).ko
endif