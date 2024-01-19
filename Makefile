# SPDX-FileCopyrightText: 2024 Michael Jeanson <mjeanson@efficios.com>
#
# SPDX-License-Identifier: GPL-2.0-only

obj-m += fsdax-test.o

KDIR = /lib/modules/$(shell uname -r)/build


all: fsdax-client
	make -C $(KDIR)  M=$(shell pwd) modules

clean:
	make -C $(KDIR)  M=$(shell pwd) clean

fsdax-client: fsdax-client.c
