include Kbuild

# KERNEL_SRC_DIR=/lib/modules/$(shell uname -r)/build
# MODULE_DIR=$(shell pwd)

default:
	$(MAKE) -C $(KERNEL_SRC_DIR) M=$(MODULE_DIR) modules

clean:
	rm -f *.o *.ko *.mod.c modules.order Module.symvers

