// SPDX-FileCopyrightText: 2024 Michael Jeanson <mjeanson@efficios.com>
//
// SPDX-License-Identifier: GPL-2.0-only

#include <linux/ioctl.h>

struct fsdax_umap {
	char *addr;
	size_t size;
};

#define FSDAX_TEST_MAJOR_NUM 101

#define FSDAX_TEST_IOCTL_MAP _IOR(FSDAX_TEST_MAJOR_NUM, 0, struct fsdax_mapping *)
#define FSDAX_TEST_IOCTL_UNMAP _IOR(FSDAX_TEST_MAJOR_NUM, 1, struct fsdax_mapping *)

/* The name of the device file */
#define FSDAX_TEST_DEVICE_FILE_NAME "fsdax-test"
