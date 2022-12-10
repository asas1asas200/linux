#ifndef __INTERNAL_VMP_H__
#define __INTERNAL_VMP_H__

#include <linux/vmprofiling.h>
#include <linux/slab.h>
#include <linux/ktime.h>
#include <linux/sched.h>

#include <trace/events/vmprofiling.h>

/* - tracepoint
 * - vmp_enter();
 * - vmp_exit();
 * - vmp_record();
 */

/* Generic definition
 */
#define vmp_get_event(group)                                                   \
	({                                                                     \
		struct vmp_event *__event;                                     \
		int __ticket = atomic_fetch_add(1, &group->ticket);            \
		if (__ticket >= group->max_nr_seq_event) {                     \
			__event = NULL;                                        \
		} else                                                         \
			__event = group->seq_events[__ticket];                 \
		__event;                                                       \
	})

#define for_each_vmp_event(group, eventpp, i)                                  \
	for (i = 0, eventpp = &group->seq_events[0];                           \
	     i < group->max_nr_seq_event; eventpp = &group->seq_events[++i])

#define vmp_event_of(vmp_eventp, type) container_of(vmp_eventp, type, vmp_event)

#define vmp_event_group_init(current, event_name, nr_event)                    \
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

#define vmp_report_max_nr_event(task)\
			pr_info("vmp: %s pid=%d tsk=%s event=%u max event\n",  \
				task->vmp_event_group->name,                   \
				task_pid_nr(task), task->comm,                 \
				task->vmp_event_group->max_nr_seq_event);      \

#define VMP_DEFINE_EVENT(name, nr_event, proto, args)                          \
	void vmp_##name##_enter(struct task_struct *task, proto)               \
	{                                                                      \
		struct vmp_event_group *group;                                 \
		if (!trace_##name##_enabled())                                 \
			return;                                                \
		if (!task)                                                     \
			task = current;                                        \
		group = vmp_event_group_init(task, name, nr_event);            \
		if (!group)                                                    \
			return;                                                \
		__vmp_##name##_enter(group, args);                             \
	}                                                                      \
	void vmp_##name##_exit(struct task_struct *task, proto)                \
	{                                                                      \
		if (!trace_##name##_enabled())                                 \
			return;                                                \
		if (!task)                                                     \
			task = current;                                        \
		if (!task->vmp_event_group)                                    \
			return;                                                \
		if (task->vmp_event_group->type != vmp_type_##name)            \
			return;                                                \
		if (atomic_read(&task->vmp_event_group->ticket) >=             \
		    task->vmp_event_group->max_nr_seq_event)                   \
			vmp_report_max_nr_event(task);                         \
		__vmp_##name##_exit(task->vmp_event_group, args);              \
		task->vmp_event_group = NULL;                                  \
	}                                                                      \
	void vmp_##name##_record(proto)                                        \
	{                                                                      \
		if (!trace_##name##_enabled())                                 \
			return;                                                \
		if (!current->vmp_event_group)                                 \
			return;                                                \
		if (current->vmp_event_group->type != vmp_type_##name)         \
			return;                                                \
		__vmp_##name##_record(current->vmp_event_group, args);         \
	}

#endif /* __INTERNAL_VMP_H__ */
