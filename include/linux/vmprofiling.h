#ifndef __VMPROFILING_H__
#define __VMPROFILING_H__

#include <linux/list.h>

enum vmp_event_flags {
	VMP_NONE = 0,
	VMP_PAGETABLE = 1,
};

struct vmp_event {
	unsigned long flags;
	atomic_t set;
	struct list_head head;
	pid_t pid;
};

struct vmp_data {
	atomic_t nr_event;
	struct list_head head;
	spinlock_t lock;
	struct vmp_event *cached_vmp_event;
};

const char *vmp_events_name = {
	[VMP_NONE] = "none",
	[VMP_PAGETABLE] = "page table",
};

void vmp_report(struct vmp_event *event, const char *fmt, ...);


#define for_each_vmp_event(event)                                              \
	list_for_each_entry_rcu (event, &vmp_data.head, vmp_event)

#define vmp_event_of(p, type) container_of(p, type, vmp_event)

// TODO: enter exit function
#define DEFINE_VMP_ENTER_EXIT_MEMBER(type, name)                               \
	struct {                                                               \
		type enter_##name##_state;                                     \
		type exit_##name##_state;                                      \
	}
#define DEFINE_VMP_COUNT_FUNC(name, type, count)                               \
	static void vmp_##name##_inc(pid_t pid)                                \
	{                                                                      \
		type *event;                                                   \
		spin_lock(&vmp_data.lock);                                     \
		if (vmp_data.cached_vmp_event &&                               \
		    vmp_data.cached_vmp_event->pid == pid) {                   \
			event = vmp_event_of(vmp_data.cached_vmp_event);       \
			atomic_long_inc(&event->count);                        \
			spin_unlock(&vmp_data.lock);                           \
			return;                                                \
		}                                                              \
		spin_unlock(&vmp_data.lock);                                   \
		rcu_read_lock();                                               \
		for_each_vmp_event (event) {                                   \
			if (event->pid == pid) {                               \
				atomic_long_dec(&event->vmp_event.count);      \
				goto found;                                    \
			}                                                      \
		}                                                              \
		spin_lock(&vmp_data.lock);                                     \
		vmp_data.cached_vmp_event = &event->vmp_event;                 \
		spin_unlock(&vmp_data.lock);                                   \
	found:                                                                 \
		rcu_read_unlock();                                             \
	}                                                                      \
	static void vmp_##name##_dec(pid_t pid)                                \
	{                                                                      \
		type *event;                                                   \
		spin_lock(&vmp_data.lock);                                     \
		if (vmp_data.cached_vmp_event &&                               \
		    vmp_data.cached_vmp_event->pid == pid) {                   \
			event = vmp_event_of(vmp_data.cached_vmp_event);       \
			atomic_long_dec(&event->count);                        \
			spin_unlock(&vmp_data.lock);                           \
			return;                                                \
		}                                                              \
		spin_unlock(&vmp_data.lock);                                   \
		rcu_read_lock();                                               \
		for_each_vmp_event (event) {                                   \
			if (event->pid == pid) {                               \
				atomic_long_dec(&event->vmp_event.count);      \
				goto found;                                    \
			}                                                      \
		spin_lock(&vmp_data.lock);                                     \
		vmp_data.cached_vmp_event = &event->vmp_event;                 \
		spin_unlock(&vmp_data.lock);                                   \
		found:                                                         \
			rcu_read_unlock();                                     \
		}

struct vmp_pagetable_event {
	struct vmp_event vmp_event;

	unsigned long addresss;
	DEFINE_VMP_ENTER_EXIT_MEMBER(pgrotval_t, prot);
	DEFINE_VMP_ENTER_EXIT_MEMBER(struct vm_area_struct, vma);
	atomic_long nr_ptes;
	atomic_long nr_pmds;
	atomic_long nr_pgds;
};

DEFINE_VMP_COUNT_FUNC(pagetable_pte, struct vmp_pagetable_event, nr_ptes);
DEFINE_VMP_COUNT_FUNC(pagetable_pmd, struct vmp_pagetable_event, nr_pmds);
DEFINE_VMP_COUNT_FUNC(pagetable_pgd, struct vmp_pagetable_event, nr_pgds);

#endif /* __VMPROFILING_H__ */
