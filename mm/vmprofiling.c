#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/debugfs.h>
#include <linux/vmprofiling.h>
#include <linux/mm_types.h>
#include <linux/swap.h>
#include <linux/swapops.h>

#include <linux/pagewalk.h>
#include <linux/ktime.h>

#include "vmprofiling.h"

#undef pr_fmt
#define pr_fmt(fmt) "vmp: " fmt

#define CREATE_TRACE_POINTS
#include <trace/events/vmprofiling.h>

/* Custom vmp event start here */

VMP_DEFINE_EVENT(pgtable, VMP_SEQ_EVENT_SIZE,
		  __VMP_PROTO(struct mm_struct *mm, int generic_type,
			      bool per_event),
		  __VMP_ARGS(mm, generic_type, per_event));

static const char *vmp_pgtable_name[] = {
	[vmp_enter] = "enter",
	[vmp_exit] = "exit",
	[vmp_copy_page_range] = "copy_page_range",
};

static inline void init_rss_vec(int *rss)
{
	memset(rss, 0, sizeof(int) * NR_MM_COUNTERS);
}

static inline int vmp_pte_entry(pte_t *pte, unsigned long addr,
			 unsigned long next, struct mm_walk *walk)
{
	struct vmp_pgtable *data = walk->private;
	struct page *page = NULL;

	if (!pte_write(*pte))
		data->nr_cow_page++;

	if (pte_present(*pte)) {
		page = vm_normal_page(walk->vma, addr, *pte);
		if (!page)
			pr_info("vmp: walker: physical page is NULL");
		else
			data->rss[mm_counter(page)]++;
	} else {
		data->nr_present_pte_entry++;
		if (is_swap_pte(*pte))
			data->nr_swap++;
	}

	return 0;
}

static inline int vmp_pmd_entry(pmd_t *pmd, unsigned long addr,
			 unsigned long next, struct mm_walk *walk)
{
	struct vmp_pgtable *data = walk->private;

	if (pmd_present(*pmd))
		data->nr_present_pmd_entry++;
	return 0;
}

static inline int vmp_pud_entry(pud_t *pud, unsigned long addr,
			 unsigned long next, struct mm_walk *walk)
{
	struct vmp_pgtable *data = walk->private;

	if (pud_present(*pud))
		data->nr_present_pud_entry++;
	data->nr_pmd++;
	return 0;
}

static inline int vmp_p4d_entry(p4d_t *p4d, unsigned long addr,
			 unsigned long next, struct mm_walk *walk)
{
	struct vmp_pgtable *data = walk->private;

	if (p4d_present(*p4d))
		data->nr_present_p4d_entry++;
	data->nr_pud++;
	return 0;
}

static inline int vmp_pgd_entry(pgd_t *pgd, unsigned long addr,
			 unsigned long next, struct mm_walk *walk)
{
	struct vmp_pgtable *data = walk->private;

	data->nr_p4d++;
	return 0;
}

static const struct mm_walk_ops vmp_walk_ops = {
	.pte_entry = vmp_pte_entry,
	.pmd_entry = vmp_pmd_entry,
	.pud_entry = vmp_pud_entry,
	.p4d_entry = vmp_p4d_entry,
	.pgd_entry = vmp_pgd_entry,
};

static inline void vmp_pgtable_event(struct vmp_event *event,
		struct mm_struct *mm, int type)
{
	struct vmp_pgtable *data = vmp_event_of(event, struct vmp_pgtable);

	init_rss_vec(data->rss);

	event->time = ktime_get();
	event->func = vmp_pgtable_name[type];

	data->pgtable_bytes = atomic_long_read(&mm->pgtables_bytes);
	data->pinned_vm = atomic64_read(&mm->pinned_vm);

	walk_page_range(mm, 0, mm->highest_vm_end, &vmp_walk_ops, data);
}

static inline void vmp_pgtable_generic_record(struct vmp_event *event,
		int type)
{
	struct vmp_pgtable_generic *data = vmp_event_of(event,
			struct vmp_pgtable_generic);

	if (unlikely(!data))
		return;

	barrier();

	if (type & vmp_pte_locked)
		data->nr_pte_locked++;
	if (type & vmp_pmd_locked)
		data->nr_pmd_locked++;
	if (type & vmp_mmap_locked)
		data->nr_mmap_locked++;
	if (type & vmp_page_table_locked)
		data->nr_page_table_locked++;
}

VMP_DEFINE_ENTER(pgtable, struct mm_struct *mm, int generic_type, bool per_event)
{
	struct vmp_event *event;
	struct vmp_pgtable_generic *generic_data;

	generic_data = kmalloc(sizeof(struct vmp_pgtable_generic), GFP_KERNEL);
	group->event = &generic_data->vmp_event;

	event = vmp_get_event(group);
	if (!event)
		return;

	vmp_pgtable_generic_record(group->event, generic_type);
	mmap_read_lock(mm);
	vmp_pgtable_event(event, mm, vmp_enter);
	mmap_read_unlock(mm);
}

/* It should hold the mmap_lock */
VMP_DEFINE_RECORD(pgtable, struct mm_struct *mm, int generic_type, bool per_event)
{
	struct vmp_event *event;
	static bool recording = false;

	if (READ_ONCE(recording))
		return;

	WRITE_ONCE(recording, true);

	if (per_event) {
		event = vmp_get_event(group);
		if (!event)
			goto out;
		vmp_pgtable_event(event, mm, generic_type);
	} else
		vmp_pgtable_generic_record(group->event, generic_type);

out:
	WRITE_ONCE(recording, false);
}

#define PGTABLE_PA(pxd) data->nr_##pxd, data->nr_present_##pxd##_entry

VMP_DEFINE_EXIT(pgtable, struct mm_struct *mm, int generic_type, bool per_event)
{
	struct vmp_event *event, **eventpp;
	unsigned int i;
	struct vmp_pgtable *data;
	struct vmp_pgtable_generic *generic_data;

	vmp_pgtable_generic_record(group->event, generic_type);
	event = vmp_get_event(group);
	if (event) {
		mmap_read_lock(mm);
		vmp_pgtable_event(event, mm, vmp_exit);
		mmap_read_unlock(mm);
	}

	generic_data = vmp_event_of(group->event, struct vmp_pgtable_generic);
	pr_info("vmp: lock pte=%lu pmd=%lu mmap=%lu page table=%llu",
			generic_data->nr_pte_locked,
			generic_data->nr_pmd_locked,
			generic_data->nr_mmap_locked,
			generic_data->nr_page_table_locked
		);

	// free data
	for_each_vmp_event (group, eventpp, i) {
		data = vmp_event_of(*eventpp, struct vmp_pgtable);
		if (i < atomic_read(&group->ticket))
			trace_pgtable(i,
				ktime_to_ns((*eventpp)->time),
				(*eventpp)->func,
				data->pgtable_bytes, data->pinned_vm,
				data->nr_swap, data->nr_cow_page, &data->rss[0],
				data->nr_present_pte_entry,
				PGTABLE_PA(pmd),
				PGTABLE_PA(pud),
				PGTABLE_PA(p4d));
		kfree(data);
	}
	kfree(group);
}

#undef PGTABLE_PA

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

static ssize_t vmp_enter_write(struct file *file, const char __user *buffer,
				 size_t len, loff_t *off)
{
	unsigned long long res;
	struct pid *pid;
	struct task_struct *task;

	int ret = kstrtoull_from_user(buffer, len, 10, &res);
	if (ret) {
		pr_err("Read pid error: %d\n", ret);
		return ret;
	} else {
		pr_info("Read pid successfully: %lld\n", res);
		pid = find_get_pid(res);
		task = pid_task(pid, PIDTYPE_PID);

		mmap_read_lock(task->mm);
		vmp_pgtable_enter(task, task->mm, 0, true);
		mmap_read_unlock(task->mm);

		*off = len;
		return len;
	}
}

static ssize_t vmp_exit_write(struct file *file, const char __user *buffer,
				 size_t len, loff_t *off)
{
	unsigned long long res;
	struct pid *pid;
	struct task_struct *task;

	int ret = kstrtoull_from_user(buffer, len, 10, &res);
	if (ret) {
		pr_err("Read pid error: %d\n", ret);
		return ret;
	} else {
		pr_info("Read pid successfully: %lld\n", res);
		pid = find_get_pid(res);
		task = pid_task(pid, PIDTYPE_PID);

		mmap_read_lock(task->mm);
		vmp_pgtable_exit(task, task->mm, 0, true);
		mmap_read_unlock(task->mm);

		*off = len;
		return len;
	}
}

static const struct file_operations vmp_pgtable_enter_fops = {
	.write = vmp_enter_write,
	.open = vmp_open,
	.release = vmp_close,
};

static const struct file_operations vmp_pgtable_exit_fops = {
	.write = vmp_exit_write,
	.open = vmp_open,
	.release = vmp_close,
};

static int __init vmp_init(void)
{
	struct dentry *vmp_dentry;

	vmp_dentry = debugfs_create_dir("vmprofiling", NULL);
	debugfs_create_file("enter", 0444, vmp_dentry, NULL,
			    &vmp_pgtable_enter_fops);
	debugfs_create_file("exit", 0444, vmp_dentry, NULL,
			    &vmp_pgtable_exit_fops);

	return 0;
}
late_initcall(vmp_init);
