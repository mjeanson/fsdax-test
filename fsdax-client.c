// SPDX-FileCopyrightText: 2024 Michael Jeanson <mjeanson@efficios.com>
//
// SPDX-License-Identifier: GPL-2.0-only

#define _GNU_SOURCE
#include <sys/mman.h>
#include <sys/stat.h>	/* For mode constants */
#include <fcntl.h>	   /* For O_* constants */
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>

#include "fsdax-test.h"

//#define SHM_FILE "/root/toto/monshm"
#define SHM_FILE "/monshm"
#define SHM_SIZE 2097152

int main(void)
{
	int fd = -1;
	int ret = 0;
	int value = 1337;
	char *mmap_addr = NULL;
	struct statx statxbuf;
	struct fsdax_umap umap;
	char buf[32];

	fd = shm_open(SHM_FILE, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP);
	if (fd < 0) {
		printf("Failed shm_open\n");
		perror("shm_open");
		ret = 1;
		goto err;
	}

	if (ftruncate(fd, SHM_SIZE) < 0) {
		printf("Failed ftruncate\n");
		perror("ftruncate");
		ret = 2;
		goto err;
	}

	if (statx(fd, "", AT_EMPTY_PATH, 0, &statxbuf) == 0) {
		printf("statx: STATX_ATTR_DAX: %d\n", (statxbuf.stx_attributes & STATX_ATTR_DAX) != 0);
	} else {
		perror("statx");
		ret = 3;
		goto err;
	}

	// Use MAP_SHARED_VALIDATE with MAP_SYNC for DAX support
	mmap_addr = mmap(NULL, SHM_SIZE, PROT_WRITE, MAP_SHARED_VALIDATE | MAP_SYNC, fd, 0);
	if (mmap_addr == MAP_FAILED) {
		printf("Failed mmap\n");
		perror("mmap");
		ret = 2;
		goto err;
	}

	// Don't need the fd after the mmap
	close(fd);

	// Write to shm
	memset(mmap_addr, 0xaa, SHM_SIZE);
	strcpy(mmap_addr, "Userspace was here!");
	printf("Wrote: '%s'\n", mmap_addr);

	fd = open("/dev/" FSDAX_TEST_DEVICE_FILE_NAME, O_RDWR);
	if(fd < 0) {
		printf("Cannot open device file...\n");
		ret = 1;
		goto err;
	}

	umap.addr = mmap_addr;
	umap.size = SHM_SIZE;

	if (ioctl(fd, FSDAX_TEST_IOCTL_MAP, &umap) < 0) {
		printf("FSDAX_TEST_IOCTL_MAP ioctl failed\n");
		ret = 1;
		goto err;
	}

	// Read what the kernel hopefuly wrote to the shm
	strncpy(buf, mmap_addr, sizeof(buf));
	buf[sizeof(buf) - 1] = '\0';

	printf("Read: '%s'\n", buf);

	// Overwrite to shm
	memset(mmap_addr, 0xbb, SHM_SIZE);
	strcpy(mmap_addr, "Userspace was here again!");

	if (ioctl(fd, FSDAX_TEST_IOCTL_UNMAP, &umap) < 0) {
		printf("FSDAX_TEST_IOCTL_UNMAP ioctl failed\n");
		ret = 1;
	}

	if (shm_unlink(SHM_FILE) != 0) {
		perror("shm_unlink");
		ret = 1;
		goto err;
	}

err:
	return ret;
}
