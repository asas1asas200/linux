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

#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>
#include <linux/pseudo_fs.h>
#include <linux/fileattr.h>
#include <linux/refcount.h>
#include <linux/sched/mm.h>
#include <uapi/linux/magic.h>
#include <uapi/linux/limits.h>
#include <uapi/linux/mman.h>

struct mshare_data {
	struct mm_struct *mm;
	refcount_t refcnt;
};

static struct super_block *msharefs_sb;

static ssize_t
mshare_read(struct kiocb *iocb, struct iov_iter *iov)
{
	struct mshare_data *info = iocb->ki_filp->private_data;
	struct mm_struct *mm = info->mm;
	size_t ret;
	struct mshare_info m_info;

	m_info.start = mm->mmap_base;
	m_info.size = mm->task_size - mm->mmap_base;
	ret = copy_to_iter(&m_info, sizeof(m_info), iov);
	if (!ret)
		return -EFAULT;
	return ret;
}

static const struct file_operations msharefs_file_operations = {
	.open		= simple_open,
	.read_iter	= mshare_read,
	.llseek		= no_llseek,
};

static int
msharefs_d_hash(const struct dentry *dentry, struct qstr *qstr)
{
	unsigned long hash = init_name_hash(dentry);
	const unsigned char *s = qstr->name;
	unsigned int len = qstr->len;

	while (len--)
		hash = partial_name_hash(*s++, hash);
	qstr->hash = end_name_hash(hash);
	return 0;
}

static struct dentry
*msharefs_alloc_dentry(struct dentry *parent, const char *name)
{
	struct dentry *d;
	struct qstr q;
	int err;

	q.name = name;
	q.len = strlen(name);

	err = msharefs_d_hash(parent, &q);
	if (err)
		return ERR_PTR(err);

	d = d_alloc(parent, &q);
	if (d)
		return d;

	return ERR_PTR(-ENOMEM);
}

static struct inode
*msharefs_get_inode(struct super_block *sb, int mode)
{
	struct inode *inode = new_inode(sb);

	if (inode) {
		inode->i_ino = get_next_ino();
		inode->i_mode = mode;

		/*
		 * msharefs are not meant to be manipulated from userspace.
		 * Reading from the file is the only allowed operation
		 */
		inode->i_flags = S_IMMUTABLE;

		inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);
		inode->i_fop = &msharefs_file_operations;

		/*
		 * A read from this file will return two unsigned long
		 */
		inode->i_size = 2 * sizeof(unsigned long);

		inode->i_uid = current_fsuid();
		inode->i_gid = current_fsgid();
	}

	return inode;
}

static int
mshare_file_create(const char *name, unsigned long flags,
			struct mshare_data *info)
{
	struct inode *inode;
	struct dentry *root, *dentry;
	int err = 0;

	root = msharefs_sb->s_root;

	inode = msharefs_get_inode(msharefs_sb, S_IFREG | 0400);
	if (IS_ERR(inode))
		return PTR_ERR(inode);

	inode->i_private = info;

	dentry = msharefs_alloc_dentry(root, name);
	if (IS_ERR(dentry)) {
		err = PTR_ERR(dentry);
		goto fail_inode;
	}

	d_add(dentry, inode);

	return err;

fail_inode:
	iput(inode);
	return err;
}

/*
 * mshare syscall
 */
SYSCALL_DEFINE5(mshare, const char __user *, name, unsigned long, addr,
		unsigned long, len, int, oflag, mode_t, mode)
{
	char mshare_name[NAME_MAX];
	struct mshare_data *info;
	struct mm_struct *mm;
	int err;

	/*
	 * Address range being shared must be aligned to pgdir
	 * boundary and its size must be a multiple of pgdir size
	 */
	if ((addr | len) & (PGDIR_SIZE - 1))
		return -EINVAL;

	err = copy_from_user(mshare_name, name, NAME_MAX);
	if (err)
		goto err_out;

	mm = mm_alloc();
	if (!mm)
		return -ENOMEM;
	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		err = -ENOMEM;
		goto err_relmm;
	}
	mm->mmap_base = addr;
	mm->task_size = addr + len;
	if (!mm->task_size)
		mm->task_size--;
	info->mm = mm;
	refcount_set(&info->refcnt, 1);

	err = mshare_file_create(mshare_name, oflag, info);
	if (err)
		goto err_relinfo;

	return 0;

err_relinfo:
	kfree(info);
err_relmm:
	mmput(mm);
err_out:
	return err;
}

/*
 * mshare_unlink syscall. Close and remove the named mshare'd object
 */
SYSCALL_DEFINE1(mshare_unlink, const char *, name)
{
	char mshare_name[NAME_MAX];
	int err;

	/*
	 * Delete the named object
	 *
	 * TODO: Mark mshare'd range for deletion
	 *
	 */
	err = copy_from_user(mshare_name, name, NAME_MAX);
	if (err)
		goto err_out;
	return 0;

err_out:
	return err;
}

static const struct dentry_operations msharefs_d_ops = {
	.d_hash = msharefs_d_hash,
};

static int
msharefs_fill_super(struct super_block *sb, struct fs_context *fc)
{
	static const struct tree_descr empty_descr = {""};
	int err;

	sb->s_d_op = &msharefs_d_ops;
	err = simple_fill_super(sb, MSHARE_MAGIC, &empty_descr);
	if (err)
		return err;

	msharefs_sb = sb;
	return 0;
}

static int
msharefs_get_tree(struct fs_context *fc)
{
	return get_tree_single(fc, msharefs_fill_super);
}

static const struct fs_context_operations msharefs_context_ops = {
	.get_tree	= msharefs_get_tree,
};

static int
mshare_init_fs_context(struct fs_context *fc)
{
	fc->ops = &msharefs_context_ops;
	return 0;
}

static struct file_system_type mshare_fs = {
	.name			= "msharefs",
	.init_fs_context	= mshare_init_fs_context,
	.kill_sb		= kill_litter_super,
};

static int
mshare_init(void)
{
	int ret = 0;

	ret = sysfs_create_mount_point(fs_kobj, "mshare");
	if (ret)
		return ret;

	ret = register_filesystem(&mshare_fs);
	if (ret)
		sysfs_remove_mount_point(fs_kobj, "mshare");

	return ret;
}

fs_initcall(mshare_init);
