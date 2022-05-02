/* Virtual Memory Profiling
 */

#include <linux/debugfs.h>
#include <linux/list.h>
#include <linux/vmprofiling.h>

#undef pr_fmt
#define pr_fmt(fmt) "vmp: " fmt

struct vmp_data vmp_data;

/* VMP - pagetable function
 *
 */

struct vmp_event *vmp_pagetable_event_init(pid_t pid, unsigned long flags)
{
	struct vmp_pagetable_event *event = NULL;

	event = kzalloc(sizeof(struct vmp_pagetable_event), GFP_KERNEL);
	if (!event)
		return NULL;

	// TODO: name
	INIT_LIST_HEAD(&event->vmp_event.head);
	event->vmp_event.pid = pid;
	event->vmp_event.flags = flags;

	INIT_VMP_COUNT_FUNC(event, nr_ptes);
	INIT_VMP_COUNT_FUNC(event, nr_pmds);
	INIT_VMP_COUNT_FUNC(event, nr_pgds);

	list_add_rcu(&event->vmp_event.head, &vmp_data.head);
	atomic_inc(&vmp_data.nr_event);

	return &event->vmp_event;
}

/* VMP generic function
 */

void vmp_report(struct vmp_event *event, const char *fmt, ...)
{

}

static inline struct vmp_event *vmp_event_init(pid_t pid, unsigned long flags)
{
	struct vmp_event *event = NULL;

	for_each_

	switch (flags) {
	case VMP_PAGETABLE:
		event = vmp_pagetable_event_init(pid, flags);
		break;
	default:
	}

	pr_info("Register %s\n", vmp_event_name[flags]);

	return event;
}

static ssize_t vmp_read(struct file *filp, char __user *buffer,
				size_t length, loff_t *offset)
{
	pr_info("read\n");
	return 0;
}

static ssize_t vmp_write(struct file *file, const char __user *buffer,
				 size_t len, loff_t *off)
{
	char *p, *start, kbuf[40];
	struct vmp_event *event = NULL;
	unsigned long flags = 0;
	static unsigned long pid = 0;

	pr_info("write\n");

	copy_from_user(kbuf, buffer, 40);
	switch (kbuf[0]) {
	case 'i':
		if (!pid)
			return -EINVAL;
		flags |= VMP_PAGETABLE;
		event = vmp_event_init((pid_t) pid, flags);
		break;
	case 'd':
		// TODO: delete
		// clear vmp_data->cached_vmp_event
	case 'p':
		for (p = &kbuf[1]; *p != '\0'; p++) {
			if (*p != ' ')
				start = p;
		}
		pid = simple_strtoul(start, p, 10);
	case 'k':
		/* TODO: kprobe */
	default:
	}

	return 0;
}

static int vmp_open(struct inode *inode, struct file *file)
{
	pr_info("open\n");
	return 0;
}
static int vmp_close(struct inode *inode, struct file *file)
{
	pr_info("close");
	return 0;
}

static const struct file_operations vmp_pgtable_fops = {
	.read = vmp_read,
	.write = vmp_write,
	.open = vmp_open,
	.release = vmp_close,
};

static int __init vmp_init(void)
{
	struct dentry *vmp_dentry;

	/* TODO: sperate the operation to mutiple files */
	vmp_dentry = debugfs_create_dir("vmprofiling", NULL);
	debugfs_create_file("vmp", 0444, vmp_dentry, NULL,
			    &vmp_pgtable_fops);

	atmoic_set(&vmp_data.nr_event, 0);
	INIT_LIST_HEAD(&vmp_data.head);

	return 0;
}
late_initcall(vmp_init);
