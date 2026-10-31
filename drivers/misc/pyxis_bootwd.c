/*
 * pyxis_bootwd: boot watchdog for unattended port bring-up.
 *
 * Enabled only with "pyxis_bootwd.enable=1" on the kernel command line.
 * If userspace has not reached system_server by stage1 seconds, or SystemUI
 * by stage2 seconds, dump the task list to the kernel log and panic; with
 * panic=N the phone reboots and pstore keeps the console for the trampoline.
 */
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/string.h>

static bool enable;
module_param(enable, bool, 0444);
static int stage1 = 300;
module_param(stage1, int, 0644);
static int stage2 = 600;
module_param(stage2, int, 0644);

static bool task_running(const char *a, const char *b)
{
	struct task_struct *p;
	bool found = false;

	rcu_read_lock();
	for_each_process(p) {
		if (!strcmp(p->comm, a) || (b && !strcmp(p->comm, b))) {
			found = true;
			break;
		}
	}
	rcu_read_unlock();
	return found;
}

static void dump_tasks(void)
{
	struct task_struct *p;

	rcu_read_lock();
	for_each_process(p)
		pr_emerg("pyxis_bootwd: pid %d state %ld comm %s\n", p->pid, p->state, p->comm);
	rcu_read_unlock();
}

static int bootwd_thread(void *unused)
{
	ssleep(stage1);
	if (!task_running("system_server", NULL)) {
		dump_tasks();
		panic("pyxis_bootwd: no system_server after %d s", stage1);
	}
	ssleep(stage2 - stage1);
	if (!task_running("ndroid.systemui", "com.android.sys")) {
		dump_tasks();
		panic("pyxis_bootwd: no SystemUI after %d s", stage2);
	}
	pr_info("pyxis_bootwd: userspace reached SystemUI, watchdog off\n");
	return 0;
}

static int __init bootwd_init(void)
{
	if (enable) {
		pr_info("pyxis_bootwd: armed, stage1 %d s, stage2 %d s\n", stage1, stage2);
		kthread_run(bootwd_thread, NULL, "pyxis_bootwd");
	}
	return 0;
}
late_initcall(bootwd_init);
