#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/sched.h>

#include <linux/ktime.h>
#include <trace/events/vmprofiling.h>

#undef pr_fmt
#define pr_fmt(fmt) "vmp: " fmt

/* - tracepoint
 * - vmp_enter();
 * - vmp_exit();
 * - vmp_record();
 */

#define vmp_event_of(vmp_eventp, type) container_of(vmp_eventp, type, vmp_event)

struct vmp_event {
	ktime_t time;
};

struct vmp_event_group {
	unsigned long type;
	char *name;

	atomic_t ticket;
	// collect all the records
	struct vmp_event *event;

	/* record once as time */
	unsigned int max_nr_seq_event;
	unsigned int nr_seq_event;
	struct vmp_event *seq_events[0];
};

/* Generic define
 */
#define vmp_get_event(group)                                                   \
	({                                                                     \
		struct vmp_event *__event;                                     \
		int __ticket = atomic_fetch_add(1, &group->ticket);             \
		if (__ticket >= group->max_nr_seq_event)                       \
			__event = NULL;                                        \
		else                                                           \
			__event = group->seq_events[__ticket];                  \
		__event;                                                       \
	})

#define for_each_vmp_event(group, eventpp, i)                                  \
	for (i = 0, eventpp = &group->seq_events[0];                            \
	     i < group->max_nr_seq_event; eventpp = &group->seq_events[++i])

enum vmp_event_group_type {
	VMP_NONE = 0,
	VMP_COPY_PAGE_RANGE = 1,
};

const char *vmp_event_group_name[2] = {
	[VMP_COPY_PAGE_RANGE] = "copy_page_range",
};

/* tracepoint - page table
 */

#define VMP_SEQ_EVENT_SIZE 4096

struct vmp_copy_page_range {
	struct vmp_event vmp_event;

	atomic_t nr_cow_page;
	atomic_t nr_pte;
	atomic_t nr_pmd;
	unsigned long pgtables_bytes;
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

	/* last event */
	event = vmp_get_event(group);
	if (!event) {
		pr_info("max event\n");
		return;
	}
	data = vmp_event_of(event, struct vmp_copy_page_range);
	data->pgtables_bytes = mm_pgtables_bytes(current->mm);
	event->time = ktime_get();

	// TODO: consolidate data

	trace_copy_page_range(data);

	// free data
	for_each_vmp_event(group, eventpp, i) {
		kfree(vmp_event_of(*eventpp, struct vmp_copy_page_range));
	}
	kfree(group);
	current->vmp_event_group = NULL;
}
