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
	mode_t mode;
	refcount_t refcnt;
};

static struct super_block *msharefs_sb;

static void
msharefs_evict_inode(struct inode *inode)
{
	clear_inode(inode);
}

static const struct super_operations msharefs_ops = {
	.statfs		= simple_statfs,
	.drop_inode	= generic_delete_inode,
	.evict_inode	= msharefs_evict_inode,
};

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
mshare_file_create(struct filename *fname, int flags,
			struct mshare_data *info)
{
	struct inode *inode;
	struct dentry *root, *dentry;
	int err = 0;

	root = msharefs_sb->s_root;

	/*
	 * This is a read only file.
	 */
	inode = msharefs_get_inode(msharefs_sb, S_IFREG | 0400);
	if (IS_ERR(inode))
		return PTR_ERR(inode);

	inode->i_private = info;

	dentry = msharefs_alloc_dentry(root, fname->name);
	if (IS_ERR(dentry)) {
		err = PTR_ERR(dentry);
		goto fail_inode;
	}

	d_add(dentry, inode);

	dput(dentry);
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
	struct mshare_data *info;
	struct mm_struct *mm;
	struct filename *fname = getname(name);
	struct dentry *dentry;
	struct inode *inode;
	struct qstr namestr;
	int err = PTR_ERR(fname);

	/*
	 * Address range being shared must be aligned to pgdir
	 * boundary and its size must be a multiple of pgdir size
	 */
	if ((addr | len) & (PGDIR_SIZE - 1))
		return -EINVAL;

	if (IS_ERR(fname))
		goto err_out;

	/*
	 * Does this mshare entry exist already? If it does, calling
	 * mshare with O_EXCL|O_CREAT is an error
	 */
	namestr.name = fname->name;
	namestr.len = strlen(fname->name);
	err = msharefs_d_hash(msharefs_sb->s_root, &namestr);
	if (err)
		goto err_out;
	inode_lock(d_inode(msharefs_sb->s_root));
	dentry = d_lookup(msharefs_sb->s_root, &namestr);
	if (dentry && (oflag & (O_EXCL|O_CREAT))) {
		err = -EEXIST;
		dput(dentry);
		goto err_unlock_inode;
	}

	if (dentry) {
		inode = d_inode(dentry);
		if (inode == NULL) {
			err = -EINVAL;
			goto err_out;
		}
		info = inode->i_private;
		refcount_inc(&info->refcnt);
		dput(dentry);
	} else {
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
		info->mode = mode;
		refcount_set(&info->refcnt, 1);
		err = mshare_file_create(fname, oflag, info);
		if (err)
			goto err_relinfo;
	}

	inode_unlock(d_inode(msharefs_sb->s_root));
	putname(fname);
	return 0;

err_relinfo:
	kfree(info);
err_relmm:
	mmput(mm);
err_unlock_inode:
	inode_unlock(d_inode(msharefs_sb->s_root));
err_out:
	putname(fname);
	return err;
}

/*
 * mshare_unlink syscall. Close and remove the named mshare'd object
 */
SYSCALL_DEFINE1(mshare_unlink, const char *, name)
{
	struct filename *fname = getname(name);
	int err = PTR_ERR(fname);
	struct dentry *dentry;
	struct inode *inode;
	struct mshare_data *info;
	struct qstr namestr;

	if (IS_ERR(fname))
		goto err_out;

	namestr.name = fname->name;
	namestr.len = strlen(fname->name);
	err = msharefs_d_hash(msharefs_sb->s_root, &namestr);
	if (err)
		goto err_out;
	inode_lock(d_inode(msharefs_sb->s_root));
	dentry = d_lookup(msharefs_sb->s_root, &namestr);
	if (dentry == NULL) {
		err = -EINVAL;
		goto err_unlock_inode;
	}

	inode = d_inode(dentry);
	if (inode == NULL) {
		err = -EINVAL;
		goto err_dput;
	}
	info = inode->i_private;

	/*
	 * Is this the last reference?
	 */
	if (refcount_dec_and_test(&info->refcnt)) {
		simple_unlink(d_inode(msharefs_sb->s_root), dentry);
		d_drop(dentry);
		d_delete(dentry);
		mmput(info->mm);
		kfree(info);
	} else {
		dput(dentry);
	}

	inode_unlock(d_inode(msharefs_sb->s_root));
	putname(fname);
	return 0;

err_dput:
	dput(dentry);
err_unlock_inode:
	inode_unlock(d_inode(msharefs_sb->s_root));
err_out:
	putname(fname);
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

	sb->s_op = &msharefs_ops;
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
