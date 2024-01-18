# SPDX-FileCopyrightText: 2024 Michael Jeanson <mjeanson@efficios.com>
#
# SPDX-License-Identifier: GPL-2.0-only

obj-m += fsdax-test.o
 
KDIR = /lib/modules/$(shell uname -r)/build
 
 
all:
	make -C $(KDIR)  M=$(shell pwd) modules
 
clean:
	make -C $(KDIR)  M=$(shell pwd) clean
