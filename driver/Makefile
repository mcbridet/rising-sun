# Makefile for the SunPCi kernel module
#
# Build Options:
#   KERNELDIR   - Path to kernel build directory (default: current kernel)
#   ARCH        - Target architecture (default: native)
#   CROSS_COMPILE - Cross compiler prefix (e.g., i686-linux-gnu-)
#
# Examples:
#   make                                    # Native build
#   make KERNELDIR=/path/to/i686/kernel    # Cross-compile with different kernel
#   make ARCH=i386                          # Build for i386/i686

# Module name
MODULE_NAME := sunpci

# If KERNELRELEASE is defined, we've been invoked from the kernel build system
ifneq ($(KERNELRELEASE),)
    obj-m := $(MODULE_NAME).o
    $(MODULE_NAME)-objs := src/main.o src/pci.o src/ioctl.o

    ccflags-y := -I$(src)/include

else
    # Otherwise we were called directly from the command line
    KERNELDIR ?= /lib/modules/$(shell uname -r)/build
    PWD := $(shell pwd)
    NPROC := $(shell nproc)

    # Build arguments for cross-compilation
    BUILD_ARGS := -C $(KERNELDIR) M=$(PWD) -j$(NPROC)
    ifdef ARCH
        BUILD_ARGS += ARCH=$(ARCH)
    endif
    ifdef CROSS_COMPILE
        BUILD_ARGS += CROSS_COMPILE=$(CROSS_COMPILE)
    endif

.PHONY: all clean install uninstall load unload

all:
	$(MAKE) $(BUILD_ARGS) modules

clean:
	$(MAKE) $(BUILD_ARGS) clean
	rm -f Module.symvers modules.order

install:
	$(MAKE) $(BUILD_ARGS) modules_install
	depmod -a

uninstall:
	rm -f /lib/modules/$(shell uname -r)/extra/$(MODULE_NAME).ko
	depmod -a

load:
	modprobe $(MODULE_NAME)

unload:
	modprobe -r $(MODULE_NAME)

endif
