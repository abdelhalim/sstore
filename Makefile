# If KERNELRELEASE is defined, we've been invoked from the
# kernel build system and can use its language.
ifneq ($(KERNELRELEASE),)
	obj-m := sstore.o
# Otherwise we were called directly from the command
# line; invoke the kernel build system.
else
	KERNELDIR ?= /lib/modules/$(shell uname -r)/build
	PWD := $(shell pwd)
default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules
endif

prog1: test_common.c test_sstore.c 
	gcc -o prog1 test_common.c test_sstore.c

prog2: test_common.c test2_sstore.c 
	gcc -o prog2 test_common.c test2_sstore.c

test: prog1 prog2
