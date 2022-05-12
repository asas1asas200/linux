#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/vmprofiling.h>

#include <linux/ktime.h>

#undef pr_fmt
#define pr_fmt(fmt) "vmp: " fmt

#define CREATE_TRACE_POINTS
#include <trace/events/vmprofiling.h>

const char *vmp_event_group_name[] = {
	[VMP_COPY_PAGE_RANGE] = "copy_page_range",
};

void vmp_copy_page_range_enter(void)
{
	struct vmp_event_group *group;
	struct vmp_event *event, **eventpp;
	struct vmp_copy_page_range *data;
	unsigned int i;

	if (!trace_copy_page_range_enabled())
		return;

	if (current->vmp_event_group) {
		/* report error, some already register */
		pr_info("pid=%d alreay regsiter event %s\n",
			task_pid_nr(current), current->vmp_event_group->name);
		return;
	}

	// TODO: alloc fail handle
	group = kzalloc(sizeof(struct vmp_event_group) +
				sizeof(struct vmp_event *) * VMP_SEQ_EVENT_SIZE,
			GFP_KERNEL);

	group->type = VMP_COPY_PAGE_RANGE;
	group->name = "copy_page_range";
	atomic_set(&group->ticket, 0);
	// TODO: set up the entry record event
	group->event = NULL;
	group->max_nr_seq_event = VMP_SEQ_EVENT_SIZE;
	group->nr_seq_event = 0;

	// TODO: alloc fail handle
	for_each_vmp_event (group, eventpp, i) {
		data = kzalloc(sizeof(struct vmp_copy_page_range), GFP_KERNEL);
		*eventpp = &data->vmp_event;
	}

	current->vmp_event_group = group;

	pr_info("%s register pid=%d tsk=%s\n", group->name, task_pid_nr(current), current->comm);

	/* first event */
	event = vmp_get_event(group);
	data = vmp_event_of(event, struct vmp_copy_page_range);
	data->pgtables_bytes = mm_pgtables_bytes(current->mm);
	event->time = ktime_get();
}

// TODO: custom parameter
void vmp_copy_page_range_record(void)
{
	struct vmp_event_group *group;
	struct vmp_event *event;
	struct vmp_copy_page_range *data;

	if (!trace_copy_page_range_enabled())
		return;

	if (!current->vmp_event_group)
		return;

	if (current->vmp_event_group->type != VMP_COPY_PAGE_RANGE)
		return;
	group = current->vmp_event_group;

	event = vmp_get_event(group);
	if (!event) {
		// TODO: provide more information
		pr_info("max event\n");
		return;
	}
	data = vmp_event_of(event, struct vmp_copy_page_range);
	data->pgtables_bytes = mm_pgtables_bytes(current->mm);
	event->time = ktime_get();
}

void vmp_copy_page_range_exit(void)
{
	struct vmp_event_group *group;
	struct vmp_event *event, **eventpp;
	struct vmp_copy_page_range *data;
	unsigned int i;

	if (!trace_copy_page_range_enabled())
		return;

	if (!current->vmp_event_group)
		return;

	if (current->vmp_event_group->type != VMP_COPY_PAGE_RANGE)
		return;
	group = current->vmp_event_group;

	pr_info("%s register pid=%d\n", group->name, task_pid_nr(current));

	/* last event */
	event = vmp_get_event(group);
	if (!event) {
		pr_info("max event\n");
		return;
	}

	BUG_ON(!current->mm);

	data = vmp_event_of(event, struct vmp_copy_page_range);
	data->pgtables_bytes = mm_pgtables_bytes(current->mm);
	event->time = ktime_get();
	current->vmp_event_group = NULL;

	// TODO: consolidate data

	pr_info("ticket %u\n", atomic_read(&group->ticket));

	for (i = 0; i < atomic_read(&group->ticket); i++) {
		event = group->seq_events[i];
		data = vmp_event_of(event, struct vmp_copy_page_range);
		trace_copy_page_range(group->name, i, data->pgtables_bytes);
	}
	// free data
	for_each_vmp_event(group, eventpp, i) {
		kfree(vmp_event_of(*eventpp, struct vmp_copy_page_range));
	}
	kfree(group);
}
