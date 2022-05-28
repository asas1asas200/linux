#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/vmprofiling.h>

#include <linux/ktime.h>

#undef pr_fmt
#define pr_fmt(fmt) "vmp: " fmt

#define CREATE_TRACE_POINTS
#include <trace/events/vmprofiling.h>

/* Custom vmp event start here */

VMP_DEFINE_ENTER(copy_page_range, struct mm_struct *mm)
{
	struct vmp_event *event;
	struct vmp_copy_page_range *data;

	/* first event */
	event = vmp_get_event(group);
	data = vmp_event_of(event, struct vmp_copy_page_range);
	data->pgtables_bytes = mm_pgtables_bytes(mm);
	event->time = ktime_get();
}

VMP_DEFINE_RECORD(copy_page_range, struct mm_struct *mm)
{
	struct vmp_event *event;
	struct vmp_copy_page_range *data;

	event = vmp_get_event(group);
	if (!event)
		return;

	data = vmp_event_of(event, struct vmp_copy_page_range);
	data->pgtables_bytes = mm_pgtables_bytes(mm);
	event->time = ktime_get();
}

VMP_DEFINE_EXIT(copy_page_range, struct mm_struct *mm)
{
	struct vmp_event *event, **eventpp;
	struct vmp_copy_page_range *data;
	unsigned int i;

	pr_info("%s register pid=%d\n", group->name, task_pid_nr(current));

	/* last event */
	event = vmp_get_event(group);
	if (!event)
		return;

	data = vmp_event_of(event, struct vmp_copy_page_range);
	data->pgtables_bytes = mm_pgtables_bytes(mm);
	event->time = ktime_get();

	// TODO: consolidate data

	pr_info("ticket %u\n", atomic_read(&group->ticket));

	for (i = 0; i < atomic_read(&group->ticket); i++) {
		event = group->seq_events[i];
		data = vmp_event_of(event, struct vmp_copy_page_range);
		trace_copy_page_range(group->name, i, data->pgtables_bytes);
	}

	// free data
	for_each_vmp_event (group, eventpp, i) {
		kfree(vmp_event_of(*eventpp, struct vmp_copy_page_range));
	}
	kfree(group);
}
