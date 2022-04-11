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
#include <linux/mman.h>
#include <linux/sched/mm.h>
#include <uapi/linux/magic.h>
#include <uapi/linux/limits.h>
#include <uapi/linux/mman.h>

struct mshare_data {
	struct mm_struct *mm, *host_mm;
	int flags;
	refcount_t refcnt;
};

ulong sysctl_mshare_size;

static struct super_block *msharefs_sb;

/* Returns holding the host mm's lock for read.  Caller must release. */
vm_fault_t
find_shared_vma(struct vm_area_struct **vmap, unsigned long *addrp)
{
	struct vm_area_struct *vma, *guest = *vmap;
	struct mshare_data *info = guest->vm_private_data;
	struct mm_struct *host_mm = info->mm;
	unsigned long host_addr;
	pgd_t *pgd, *guest_pgd;

	host_addr = *addrp - guest->vm_start + host_mm->mmap_base;
	pgd = pgd_offset(host_mm, host_addr);
	guest_pgd = pgd_offset(current->mm, *addrp);
	if (!pgd_same(*guest_pgd, *pgd)) {
		set_pgd(guest_pgd, *pgd);
		return VM_FAULT_NOPAGE;
	}

	*addrp = host_addr;
	mmap_read_lock(host_mm);
	vma = find_vma(host_mm, host_addr);

	/* XXX: expand stack? */
	if (vma && vma->vm_start > host_addr)
		vma = NULL;

	*vmap = vma;
	return 0;
}



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
*msharefs_get_inode(struct super_block *sb, mode_t mode)
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
mshare_file_create(struct filename *fname, mode_t mode,
			struct mshare_data *info)
{
	struct inode *inode;
	struct dentry *root, *dentry;
	int err = 0;
	mode_t fmode;

	root = msharefs_sb->s_root;

	/*
	 * This is a read only file so mask out all other bits. Make sure
	 * it is readable by owner at least.
	 */
	fmode = (mode & 0444) | S_IFREG | 0400;
	inode = msharefs_get_inode(msharefs_sb, fmode);
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
	struct filename *fname = getname(name);
	struct dentry *dentry;
	struct inode *inode;
	struct qstr namestr;
	struct vm_area_struct *vma, *next, *new_vma;
	struct mm_struct *new_mm;
	unsigned long end;
	int err = PTR_ERR(fname);

	/*
	 * Is msharefs mounted? TODO: If not mounted, return error
	 * or automount?
	 */
	if (msharefs_sb == NULL)
		return -ENOENT;

	/*
	 * Address range being shared must be aligned to pgdir
	 * boundary and its size must be a multiple of pgdir size
	 */
	if ((addr | len) & (PGDIR_SIZE - 1))
		return -EINVAL;

	if (IS_ERR(fname))
		goto err_out;

	end = addr + len;

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
		inode = d_inode(dentry);
		err = -EEXIST;
		dput(dentry);
		goto err_unlock_inode;
	}
	oflag &= (O_RDONLY | O_WRONLY | O_RDWR);

	if (dentry) {
		unsigned long mapaddr, prot = PROT_NONE;

		/*
		 * If a task is trying to map in an existing mshare'd
		 * range, make sure there are no overlapping mappings
		 * in calling process already
		 */
		mmap_read_lock(current->mm);
		vma = find_vma_intersection(current->mm, addr, end);
		if (vma) {
			mmap_read_unlock(current->mm);
			err = -EINVAL;
			goto err_unlock_inode;
		}
		mmap_read_unlock(current->mm);

		inode = d_inode(dentry);
		if (inode == NULL) {
			err = -EINVAL;
			goto err_unlock_inode;
		}
		info = inode->i_private;
		dput(dentry);

		/*
		 * Map in the address range as anonymous mappings
		 */
		if (oflag != (oflag & info->flags)) {
			err = -EPERM;
			goto err_unlock_inode;
		}

		if (oflag & O_RDONLY)
			prot |= PROT_READ;
		else if (oflag & O_WRONLY)
			prot |= PROT_WRITE;
		else if (oflag & O_RDWR)
			prot |= (PROT_READ | PROT_WRITE);
		mapaddr = vm_mmap(NULL, addr, len, prot,
				MAP_FIXED | MAP_SHARED | MAP_ANONYMOUS, 0);
		if (IS_ERR((void *)mapaddr)) {
			err = -EINVAL;
			goto err_unlock_inode;
		}

		refcount_inc(&info->refcnt);

		/*
		 * Now that we have mmap'd the mshare'd range, update vma
		 * flags and vm_mm pointer for this mshare'd range.
		 */
		mmap_write_lock(current->mm);
		vma = find_vma(current->mm, addr);
		if (vma && vma->vm_start < addr) {
			mmap_write_unlock(current->mm);
			err = -EINVAL;
			goto err_unlock_inode;
		}

		while (vma && vma->vm_start < (addr + len)) {
			vma->vm_private_data = info;
			vma->vm_mm = info->mm;
			vma->vm_flags |= VM_SHARED_PT;
			next = vma->vm_next;
			vma = next;
		}
		mmap_write_unlock(current->mm);
	} else {
		unsigned long myaddr;
		struct mm_struct *old_mm;

		old_mm = current->mm;
		new_mm = mm_alloc();
		if (!new_mm)
			return -ENOMEM;
		info = kzalloc(sizeof(*info), GFP_KERNEL);
		if (!info) {
			err = -ENOMEM;
			goto err_relmm;
		}
		new_mm->mmap_base = addr;
		new_mm->task_size = addr + len;
		if (!new_mm->task_size)
			new_mm->task_size--;
		info->mm = new_mm;
		info->host_mm = old_mm;
		info->flags = oflag;
		refcount_set(&info->refcnt, 1);

		/*
		 * VMAs for this address range may or may not exist.
		 * If VMAs exist, they should be marked as shared at
		 * this point and page table info should be copied
		 * over to newly created mm_struct. TODO: If VMAs do not
		 * exist, create them and mark them as shared.
		 */
		mmap_read_lock(old_mm);
		vma = find_vma_intersection(old_mm, addr, end);
		if (!vma) {
			mmap_read_unlock(old_mm);
			err = -EINVAL;
			goto free_info;
		}
		/*
		 * TODO: If the currently allocated VMA goes beyond the
		 * mshare'd range, this VMA needs to be split.
		 *
		 * Double check that source VMAs do not extend outside
		 * the range
		 */
		vma = find_vma(old_mm, addr + len);
		if (vma && vma->vm_start < (addr + len)) {
			mmap_read_unlock(old_mm);
			err = -EINVAL;
			goto free_info;
		}

		vma = find_vma(old_mm, addr);
		if (vma && vma->vm_start < addr) {
			mmap_read_unlock(old_mm);
			err = -EINVAL;
			goto free_info;
		}
		mmap_read_unlock(old_mm);

		mmap_write_lock(new_mm);
		mmap_write_lock(old_mm);
		while (vma && vma->vm_start < (addr + len)) {
			/*
			 * Copy this vma over to host mm
			 */
			vma->vm_private_data = info;
			vma->vm_flags |= VM_SHARED_PT;
			new_vma = vm_area_dup(vma);
			if (!new_vma) {
				mmap_write_unlock(new_mm);
				mmap_write_unlock(old_mm);
				err = -ENOMEM;
				goto free_info;
			}
			new_vma->vm_mm = new_mm;
			err = insert_vm_struct(new_mm, new_vma);
			if (err) {
				mmap_write_unlock(new_mm);
				mmap_write_unlock(old_mm);
				err = -ENOMEM;
				goto free_info;
			}

			/* Copy over current PTEs */
			err = mshare_copy_ptes(new_vma, vma);
			if (err != 0)
				goto free_info;
			vma = vma->vm_next;
		}

		/*
		 * TODO: Free the corresponding page table in calling
		 * process
		 */
		mmap_write_unlock(old_mm);
		mmap_write_unlock(new_mm);

		err = mshare_file_create(fname, mode, info);
		if (err)
			goto free_info;
	}

	inode_unlock(d_inode(msharefs_sb->s_root));
	putname(fname);
	return 0;

free_info:
	kfree(info);
err_relmm:
	mmput(new_mm);
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

	if (msharefs_sb == NULL)
		return -ENOENT;

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
	 * TODO: permission checks are needed before proceeding
	 */
	if (refcount_dec_and_test(&info->refcnt)) {
		simple_unlink(d_inode(msharefs_sb->s_root), dentry);
		d_drop(dentry);
		d_delete(dentry);
		/*
		 * TODO: Release all physical pages allocated for this
		 * mshare range and release associated page table. If
		 * the final unlink happens from the process that created
		 * mshare'd range, do we return page tables and pages to
		 * that process so the creating process can continue using
		 * the address range it had chosen to mshare at some
		 * point?
		 *
		 * TODO: unmap shared vmas from every task that is using
		 * this mshare'd range.
		 */
		mmput(info->mm);
		kfree(info);
	} else {
		/*
		 * TODO: If mshare'd range is still mapped in the process,
		 * it should be unmapped. Following is minimal code and
		 * might need fix up
		 */
		unsigned long tmp;

		tmp = info->mm->task_size - info->mm->mmap_base;
		if (info->host_mm != current->mm)
			vm_munmap(info->mm->mmap_base, tmp);

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

	sysctl_mshare_size = PGDIR_SIZE;
	return ret;
}

fs_initcall(mshare_init);
