CONFIG_MODULE_FORCE_UNLOAD=y

# debug build:
# "CFLAGS was changed ... Fix it to use EXTRA_CFLAGS."
EXTRA_CFLAGS=-Wall -Wmissing-prototypes -Wstrict-prototypes -g -O2

BUILDSYSTEM_DIR:=/home/volumio/Documenti/VOLUMIO/sunxi-linux-5.0

obj-m += snd-fifo.o

snd-fifo-objs  := fifo.o

all:
	# make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) modules
	make ARCH=arm64 CROSS_COMPILE=/opt/toolchain/gcc-linaro-6.3.1-2017.02-x86_64_aarch64-linux-gnu/bin/aarch64-linux-gnu- -C $(BUILDSYSTEM_DIR) M=$(shell pwd) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) clean
