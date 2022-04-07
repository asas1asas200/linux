/* Virtual Memory Profiling
 *
 */

#include  <linux/debugfs.h>

#undef pr_fmt
#define pr_fmt(fmt) "vmp: " fmt

static ssize_t vmp_pgtable_read(struct file *filp, char __user *buffer,
                           size_t length, loff_t *offset)
{
	pr_info("read\n");
	return 0;
}

static ssize_t vmp_pgtable_write(struct file *file, const char __user *buffer,
                            size_t len, loff_t *off)
{
	pr_info("write\n");
	return -EINVAL;
}

static int vmp_pgtable_open(struct inode *inode, struct file *file)
{
	pr_info("open\n");
    	return 0;
}
static int vmp_pgtable_close(struct inode *inode, struct file *file)
{
	pr_info("close");
	return 0;
}

static const struct file_operations vmp_pgtable_fops = {
	.read = vmp_pgtable_read,
	.write = vmp_pgtable_write,
	.open = vmp_pgtable_open,
	.release = vmp_pgtable_close,
};

static int __init vmp_init(void)
{
	struct dentry *vmp_dentry;

	vmp_dentry = debugfs_create_dir("vmprofiling", NULL);
	debugfs_create_file("pgtable", 0444, vmp_dentry, NULL, &vmp_pgtable_fops);

	return 0;
}
late_initcall(vmp_init);
