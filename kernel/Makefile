obj-m += somagic.o

somagic-y += somagic_main.o somagic_bootloader.o

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)
all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	@rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions modules.order Module.symvers

