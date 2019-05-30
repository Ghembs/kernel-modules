TARGET_MODULE:=kernel-encoding

obj-m += kernel-encoding.o

$(TARGET_MODULE)-objs  := kernel_encoding.o
BUILDSYSTEM_DIR:=/lib/modules/$(shell uname -r)/build
PWD:=$(shell pwd)

all :
	$(MAKE) -C $(BUILDSYSTEM_DIR) M=$(PWD) modules
clean:
	$(MAKE) -C $(BUILDSYSTEM_DIR) M=$(PWD) clean
load:
	/sbin/insmod ./$(TARGET_MODULE).ko
unload:
	/sbin/rmmod ./$(TARGET_MODULE).ko
