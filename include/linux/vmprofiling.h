#ifndef __VMPROFILING_H__
#define __VMPROFILING_H__

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

#define VMP_DECLARE_ENTER(name, args...)                                       \
	void vmp_##name##_enter(struct task_struct *task, args);               \
	void __vmp_##name##_enter(struct vmp_event_group *group, args)
#define VMP_DECLARE_EXIT(name, args...)                                        \
	void vmp_##name##_exit(struct task_struct *task, args);                 \
	void __vmp_##name##_exit(struct vmp_event_group *group, args)
#define VMP_DECLARE_RECORD(name, args...)                                      \
	void vmp_##name##_record(args);                                         \
	void __vmp_##name##_record(struct vmp_event_group *group, args)

#define VMP_DECLARE_EVENT(name, nr_event, proto, args)                         \
	VMP_DECLARE_ENTER(name, proto);\
	VMP_DECLARE_EXIT(name, proto);\
	VMP_DECLARE_RECORD(name, proto)

#define __VMP_PROTO(args...) args
#define __VMP_ARGS(args...) args

enum vmp_event_group_type {
	vmp_type_pgtable = 0,
	/* Add new vmp event number here */
};

/* tracepoint - page table
 */

#define VMP_SEQ_EVENT_SIZE 4096

/* custom structure need to be like vmp_event_name */
struct vmp_pgtable_generic {
	struct vmp_event vmp_event;

	unsigned int nr_pte_locked;
	unsigned int nr_pmd_locked;
	unsigned int nr_mmap_locked;
	unsigned int nr_page_table_locked;
};

#define VMP_PGTABLE_DECLARE(type) vmp_##type
enum {
	VMP_PGTABLE_DECLARE(pte_locked) = 0x001,
	VMP_PGTABLE_DECLARE(pmd_locked) = 0x002,
	VMP_PGTABLE_DECLARE(mmap_locked) = 0x004,
	VMP_PGTABLE_DECLARE(page_table_locked) = 0x008,
};

#undef VMP_PGTABLE_DECLARE

struct vmp_pgtable {
	struct vmp_event vmp_event;

	/* can get from mm_struct */
	unsigned int pgtable_bytes;
	unsigned int pinned_vm;

	/* needs to walk */
	unsigned int nr_swap;
	unsigned int nr_cow_page;
	struct mm_rss_stat rss;

#define VMP_PGTABLE_DECLARE(pxd)                                               \
	unsigned int nr_##pxd;                                                 \
	unsigned int nr_present_##pxd##_entry;

	unsigned int nr_present_pte_entry;
	VMP_PGTABLE_DECLARE(pmd);
	VMP_PGTABLE_DECLARE(pud);
	VMP_PGTABLE_DECLARE(p4d);

#undef VMP_PGTABLE_DECLARE
};

VMP_DECLARE_EVENT(pgtable, VMP_SEQ_EVENT_SIZE,
		  __VMP_PROTO(struct mm_struct *mm, int generic_type,
			      bool per_event),
		  __VMP_ARGS(mm, generic_type, per_event));

#endif /* __VMPROFILING_H__ */
