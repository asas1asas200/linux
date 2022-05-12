#ifndef __VMPROFILING_H__
#define __VMPROFILING_H__

#include <linux/ktime.h>

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

extern const char *vmp_event_group_name[];

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

void vmp_copy_page_range_enter(void);
void vmp_copy_page_range_exit(void);
void vmp_copy_page_range_record(void);

#endif /* __VMPROFILING_H__ */
