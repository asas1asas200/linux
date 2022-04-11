// SPDX-License-Identifier: GPL-2.0-only
/*
 * mm/mshare.c
 *
 * Page table sharing code
 *
 *
 * Copyright (C) 2021 Oracle Corp. All rights reserved.
 * Authors:	Khalid Aziz <khalid.aziz@oracle.com>
 *		Matthew Wilcox <willy@infradead.org>
 */

#include <linux/anon_inodes.h>
#include <linux/fs.h>
#include <linux/syscalls.h>

static const struct file_operations mshare_fops = {
};

/*
 * mshare syscall. Returns a file descriptor
 */
SYSCALL_DEFINE5(mshare, const char *, name, unsigned long, addr,
		unsigned long, len, int, oflag, mode_t, mode)
{
	int fd;

	/*
	 * Address range being shared must be aligned to pgdir
	 * boundary and its size must be a multiple of pgdir size
	 */
	if ((addr | len) & (PGDIR_SIZE - 1))
		return -EINVAL;

	/*
	 * Allocate a file descriptor to return
	 *
	 * TODO: This code ignores the object name completely. Add
	 * support for that
	 */
	fd = anon_inode_getfd("mshare", &mshare_fops, NULL, O_RDWR);

	return fd;
}

/*
 * mshare_unlink syscall. Close and remove the named mshare'd object
 */
SYSCALL_DEFINE1(mshare_unlink, const char *, name)
{
	int fd;

	/*
	 * Delete the named object
	 *
	 * TODO: Mark mshare'd range for deletion
	 *
	 */
	return 0;
}
