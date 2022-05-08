#include <linux/debugfs.h>
#include <linux/list.h>
#include <linux/rculist.h>
#include <linux/types.h>
#include <linux/kstrtox.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/pid.h>
#include <linux/vmprofiling.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/rcupdate.h>

static struct dentry *vmp_dentry;
pid_t vmp_pid_nr;

struct vmp_data {
	struct list_head event_groups;
	struct list_head event_group_types;
	spinlock_t lock;
} vmp_data;

static int register_event_group(struct vmp_event_group_type *event_group_type)
{
	spin_lock(&vmp_data.lock);
	list_add_rcu(event_group_type, &vmp_data.event_group_types);
	spin_unlock(&vmp_data.lock);
}
EXPORT_SYMBOL_GPL(register_event_group);

static int unregister_event_group(char *event_group)
{
}

static void
active_new_event_group(struct vmp_event_group_type *event_group_type)
{
	int event_idx;
	struct vmp_event_group *event_group;
	struct vmp_event *iter_event, *new_event;
	struct dentry *events_dir, *event_dir;
	char dir_name[56];

	/* Create basic event_group debugfs */
	event_group = kzalloc(sizeof(struct vmp_event_group), GFP_KERNEL);
	event_group->nr_event = 0;
	event_group->pid = vmp_pid_nr;
	sprintf(dir_name, "%d_%s", vmp_pid_nr, event_group_type->name);
	event_group->dentry = debugfs_create_dir(dir_name, vmp_dentry);
	strncpy(event_group->group_name, event_group_type->name,
		strlen(event_group_type->name));
	pr_info("New vmp_event_group: \n\tnr_event: %d\n\tpid: %d\n\tgroup_name: %s\n",
		event_group->nr_event, event_group->pid,
		event_group->group_name);

	events_dir = debugfs_create_dir("events", event_group->dentry);
	debugfs_create_file("events_information", 0444, event_group->dentry,
			    NULL, NULL);

	/* Initialize events */
	event_group->events = kcalloc(ARRAY_SIZE(event_group_type->events),
				      sizeof(struct vmp_event), GFP_KERNEL);
	for (event_idx = 0; event_group_type->events[event_idx] != NULL;
	     event_idx++) {
		event_group->events[event_idx] =
			event_group_type->events[event_idx];
		event_dir = debugfs_create_dir(
			event_group->events[event_idx]->name, events_dir);
		debugfs_create_file(
			"report_information", 0444, event_dir, NULL,
			event_group->events[event_idx]->ops->report);
		debugfs_create_file(
			"online", 0222, event_dir, NULL,
			event_group->events[event_idx]->ops->online);
		debugfs_create_file(
			"offline", 0222, event_dir, NULL,
			event_group->events[event_idx]->ops->offline);
	}

	spin_lock(&vmp_data.lock);
	list_add_tail_rcu(&event_group->list_head, &vmp_data.event_groups);
	task->event_group = event_group;
	spin_unlock(&vmp_data.lock);
}

static ssize_t active_builtin_event_group_write(struct file *file,
						const char __user *buffer,
						size_t len, loff_t *off)

{
	int ret = 0;
	struct task_struct *task;
	struct pid *pid;
	struct vmp_event_group *event_group;
	struct vmp_event_group_type *event_group_type;
	char event_group_name[40];
	// The max size is calculated by: 40 + '_' + len(INT_MAX) + 1 = 56
	char dir_name[56];
	memset(event_group_name, '\0', 40);

	if (len > 40) {
		pr_err("Event name too long.");
		goto err_inval;
	}

	if (!vmp_pid_nr) {
		pr_err("Pid unspecified.\n");
		goto err_inval;
	}

	ret = copy_from_user(event_group_name, buffer, len);
	event_group_name[len - 1] = '\0'; // remove trailing '\n'
	*off = len;

	/* Check if process exists. */
	pid = find_get_pid(vmp_pid_nr);

	if (pid == NULL) {
		pr_err("Pid %d doesn't exist.\n", vmp_pid_nr);
		goto err_inval;
	}

	task = pid_task(pid, PIDTYPE_PID);

	/* Check if event_group_type exists. */
	rcu_read_lock();

	list_for_each_entry_rcu (event_group_type, &vmp_data.event_group_types,
				 list_head) {
		if (strcmp(event_group_type, event_group_name) == 0) {
			goto type_found;
		}
	}

	rcu_read_unlock();

	pr_err("event group type %d not found", event_group_name);
	goto err_inval;

type_found:
	rcu_read_unlock();

	active_new_event_group(event_group_type);

	pr_info("Create new event group: %s\n", dir_name);

	vmp_pid_nr = 0;
	if (ret)
		return ret;
	return len;

err_inval:
	vmp_pid_nr = 0;
	return -EINVAL;
}

static ssize_t inactive_builtin_event_group_write(struct file *file,
						  const char __user *buffer,
						  size_t len, loff_t *off)
{
	int ret = 0;
	struct vmp_event_group *target;
	char event_group_name[40];
	memset(event_group_name, '\0', 40);

	if (len > 40) {
		pr_err("Event name too long.");
		goto err_inval;
	}

	if (!vmp_pid_nr) {
		pr_err("Pid unspecified.\n");
		goto err_inval;
	}

	ret = copy_from_user(event_group_name, buffer, len);
	event_group_name[len - 1] = '\0'; // remove trailing '\n'
	*off = len;

	spin_lock(&vmp_data.lock);
	list_for_each_entry_rcu (target, &vmp_data.event_groups, list_head) {
		if (target->pid == vmp_pid_nr &&
		    strcmp(target->group_name, event_group_name) == 0)
			goto target_found;
	}

	pr_err("event_group %s with pid %d not found.\n", event_group_name,
	       vmp_pid_nr);
	spin_unlock(&vmp_data.lock);
	goto err_inval;

target_found:
	list_del_rcu(&target->list_head);
	spin_unlock(&vmp_data.lock);
	debugfs_remove_recursive(target->dentry);

	synchronize_rcu();
	kfree(target);

	pr_info("event_group %s with pid %d has been removed.\n",
		event_group_name, vmp_pid_nr);

	vmp_pid_nr = 0;
	return len;

err_inval:
	vmp_pid_nr = 0;
	return -EINVAL;
}

static ssize_t pid_write(struct file *file, const char __user *buffer,
			 size_t len, loff_t *off)
{
	unsigned long long res;
	int ret = kstrtoull_from_user(buffer, len, 10, &res);
	if (ret) {
		vmp_pid_nr = 0;
		pr_err("Read pid error: %d\n", ret);
		return ret;
	} else {
		vmp_pid_nr = res;
		pr_info("Read pid successfully: %d\n", vmp_pid_nr);
		*off = len;
		return len;
	}
}

static const struct file_operations pid_fops = { .write = pid_write };

static const struct file_operations active_builtin_event_group_fops = {
	.write = active_builtin_event_group_write,
};

static const struct file_operations inactive_builtin_event_group_fops = {
	.write = inactive_builtin_event_group_write,
};

static int __init vmp_init(void)
{
	spin_lock_init(&vmp_data.lock);

	vmp_dentry = debugfs_create_dir("vmprofiling", NULL);
	debugfs_create_file("active_builtin_event_group", 0222, vmp_dentry,
			    NULL, &active_builtin_event_group_fops);
	debugfs_create_file("inactive_builtin_event_group", 0222, vmp_dentry,
			    NULL, &inactive_builtin_event_group_fops);
	debugfs_create_file("pid", 0222, vmp_dentry, NULL, &pid_fops);

	INIT_LIST_HEAD(&vmp_data.event_groups);
	INIT_LIST_HEAD(&vmp_data.event_group_types);

	return 0;
}

late_initcall(vmp_init);