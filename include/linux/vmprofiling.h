#ifndef __VMPROFILING_H__
#define __VMPROFILING_H__

#include <linux/list.h>
#include <linux/errno.h>
#include <linux/types.h>

struct vmp_event_ops {
	int (*report)(struct vmp_event *);
	int (*record)(struct vmp_event *);
	int (*active)(struct vmp_event *);
	int (*online)(struct vmp_event *);
	int (*offline)(struct vmp_event *);
};

struct vmp_event {
	char *name;
	int nr_recorded;
	bool is_online;
	void *data;
	struct vmp_event_ops *ops;
	spinlock_t lock;
};

struct vmp_event_group {
	int nr_event;
	int pid;
	char group_name[40];
	struct dentry *dentry;

	struct list_head list_head;
	struct vmp_event events[];
};

struct vmp_event_group_ops {
	void (*get_events_info)(void);
	void (*release)(void);
};

struct vmp_event_group_type {
	char *name;
	struct list_head list_head;
	struct vmp_event events[];
	struct vmp_event_group_ops *ops;
};

#endif