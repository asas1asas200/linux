#ifndef __VMPROFILING_H__
#define __VMPROFILING_H__

#include <trace/events/vmprofiling.h>
#include <linux/ktime.h>
#include <linux/sched.h>

/* - tracepoint
 * - vmp_enter();
 * - vmp_exit();
 * - vmp_record();
 */

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
	struct vmp_event *seq_events[0];
};

/* Generic definition
 */
#define vmp_get_event(group)                                                   \
	({                                                                     \
		struct vmp_event *__event;                                     \
		int __ticket = atomic_fetch_add(1, &group->ticket);            \
		if (__ticket >= group->max_nr_seq_event) {                     \
			pr_info("%s pid=%d tsk=%s event=%u max event\n",       \
				group->name, task_pid_nr(current),             \
				current->comm, group->max_nr_seq_event);       \
			__event = NULL;                                        \
		} else                                                         \
			__event = group->seq_events[__ticket];                 \
		__event;                                                       \
	})

#define for_each_vmp_event(group, eventpp, i)                                  \
	for (i = 0, eventpp = &group->seq_events[0];                           \
	     i < group->max_nr_seq_event; eventpp = &group->seq_events[++i])

#define vmp_event_of(vmp_eventp, type) container_of(vmp_eventp, type, vmp_event)

#define vmp_event_group_init(event_name, nr_event)                             \
	({                                                                     \
		struct vmp_event_group *group = NULL;                          \
		struct vmp_event **eventpp;                                    \
		struct vmp_##event_name *data;                                 \
		unsigned int i, j;                                             \
                                                                               \
		if (!trace_##event_name##_enabled())                           \
			goto done;                                             \
                                                                               \
		if (current->vmp_event_group) {                                \
			/* report error, some already register */              \
			pr_info("pid=%d already regsiter event %s\n",          \
				task_pid_nr(current),                          \
				current->vmp_event_group->name);               \
			goto done;                                             \
		}                                                              \
                                                                               \
		group = kzalloc(sizeof(struct vmp_event_group) +               \
					sizeof(struct vmp_event *) * nr_event, \
				GFP_KERNEL);                                   \
		if (!group)                                                    \
			goto done;                                             \
                                                                               \
		data = kzalloc(sizeof(typeof(*data)), GFP_KERNEL);             \
		if (!data)                                                     \
			goto free_group;                                       \
                                                                               \
		group->event = &data->vmp_event;                               \
		group->type = vmp_type_##event_name;                           \
		group->name = #event_name;                                     \
		atomic_set(&group->ticket, 0);                                 \
		group->max_nr_seq_event = nr_event;                            \
                                                                               \
		for_each_vmp_event (group, eventpp, i) {                       \
			data = kzalloc(sizeof(typeof(*data)), GFP_KERNEL);     \
			if (!data)                                             \
				goto free_events;                              \
			*eventpp = &data->vmp_event;                           \
		}                                                              \
                                                                               \
		current->vmp_event_group = group;                              \
                                                                               \
		pr_info("%s register pid=%d tsk=%s event=%u\n", group->name,   \
			task_pid_nr(current), current->comm,                   \
			group->max_nr_seq_event);                              \
		goto done;                                                     \
                                                                               \
	free_events:                                                           \
		for (j = 0; j <= i; j++)                                       \
			kfree(group->seq_events[j]);                           \
		kfree(group->event);                                           \
	free_group:                                                            \
		kfree(group);                                                  \
		group = NULL;                                                  \
		pr_info("allocate failed\n");                                  \
	done:                                                                  \
		group;                                                         \
	})

#define VMP_DEFINE_ENTER(name, args...)                                        \
	void __vmp_##name##_enter(struct vmp_event_group *group, args)
#define VMP_DEFINE_EXIT(name, args...)                                         \
	void __vmp_##name##_exit(struct vmp_event_group *group, args)
#define VMP_DEFINE_RECORD(name, args...)                                       \
	void __vmp_##name##_record(struct vmp_event_group *group, args)

#define VMP_DECLARE_EVENT(name, nr_event, proto, args)                         \
	VMP_DEFINE_ENTER(name, proto);                                         \
	VMP_DEFINE_EXIT(name, proto);                                          \
	VMP_DEFINE_RECORD(name, proto);                                        \
	static inline void vmp_##name##_enter(proto)                           \
	{                                                                      \
		struct vmp_event_group *group;                                 \
		if (!trace_##name##_enabled())                                 \
			return;                                                \
		group = vmp_event_group_init(name, nr_event);                  \
		if (!group)                                                    \
			return;                                                \
		__vmp_##name##_enter(group, args);                             \
	}                                                                      \
	static inline void vmp_##name##_exit(proto)                            \
	{                                                                      \
		if (!trace_##name##_enabled())                                 \
			return;                                                \
		if (!current->vmp_event_group)                                 \
			return;                                                \
		if (current->vmp_event_group->type != vmp_type_##name)         \
			return;                                                \
		__vmp_##name##_exit(current->vmp_event_group, args);           \
		current->vmp_event_group = NULL;                               \
	}                                                                      \
	static inline void vmp_##name##_record(proto)                          \
	{                                                                      \
		if (!trace_##name##_enabled())                                 \
			return;                                                \
		if (!current->vmp_event_group)                                 \
			return;                                                \
		if (current->vmp_event_group->type != vmp_type_##name)         \
			return;                                                \
		__vmp_##name##_record(current->vmp_event_group, args);         \
	}

#define __VMP_PROTO(args...) args
#define __VMP_ARGS(args...) args

enum vmp_event_group_type {
	vmp_type_copy_page_range = 0,
	/* Add new vmp event number here */
};

/* tracepoint - page table
 */

#define VMP_SEQ_EVENT_SIZE 40

/* custom structure need to be like vmp_event_name  */
struct vmp_copy_page_range {
	struct vmp_event vmp_event;

	atomic_t nr_cow_page;
	atomic_t nr_pte;
	atomic_t nr_pmd;
	unsigned long pgtables_bytes;
};

VMP_DECLARE_EVENT(copy_page_range, VMP_SEQ_EVENT_SIZE,
		  __VMP_PROTO(struct mm_struct *mm), __VMP_ARGS(mm));

#endif /* __VMPROFILING_H__ */
