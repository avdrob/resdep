#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
#include <linux/sched/types.h>
#endif

#define MS_TO_NS(x) (x * 1E6L)
#define MS_TO_US(x) (x * 1E3L)

struct hog_thread_data {
	struct task_struct *hog_thread;
	struct hrtimer hog_hrtimer;
	int cpu;
	bool is_running;
};

static struct task_struct *hog_thread;
static struct hrtimer hog_hrtimer;
static bool is_running;

const unsigned long work_time_ms = 200L;
const unsigned long sleep_time_ms = 800L;

enum hrtimer_restart hog_hrtimer_callback(struct hrtimer *timer )
{
	printk(KERN_INFO "[kcpuhog]: hrtimer_callback is called\n");

	if (is_running) {
		is_running = false;
		hrtimer_forward_now(timer,
				ktime_set(0, MS_TO_NS(sleep_time_ms)));
	}
	else {
		is_running = true;
		hrtimer_forward_now(timer,
				ktime_set(0, MS_TO_NS(work_time_ms)));
	}

	return HRTIMER_RESTART;
}

static int hog_thread_fn(void *data)
{
	// unsigned long start_jiffies, tmo;

	hrtimer_init(&hog_hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	hog_hrtimer.function = hog_hrtimer_callback;
	hrtimer_start(&hog_hrtimer, ktime_set(0, MS_TO_NS(work_time_ms)),
			HRTIMER_MODE_REL);

	is_running = true;
	while (!kthread_should_stop()) {
		printk(KERN_INFO "[kcpuhog]: begin busyloop\n");
		while (is_running)
			cpu_relax();
		printk(KERN_INFO "[kcpuhog]: end busyloop\n");

		printk(KERN_INFO "[kcpuhog]: begin sleep\n");
		usleep_range(MS_TO_US(sleep_time_ms), MS_TO_US(sleep_time_ms));
		printk(KERN_INFO "[kcpuhog]: end sleep\n");
	}

	printk(KERN_INFO "[kcpuhog]: Cancel timer\n");
	if (hrtimer_cancel(&hog_hrtimer))
		printk(KERN_INFO "[kcpuhog]: The timer was active\n");

	/*
restart:

	start_jiffies = jiffies;
	tmo = msecs_to_jiffies(work_time_ms);
	printk(KERN_INFO "[kcpuhog]: begin busyloop\n");
	while (jiffies < start_jiffies + tmo)
		cpu_relax();
	printk(KERN_INFO "[kcpuhog]: end busyloop\n");

	if (kthread_should_stop())
		goto term;

	printk(KERN_INFO "[kcpuhog]: begin sleep\n");
	usleep_range(MS_TO_US(sleep_time_ms), MS_TO_US(sleep_time_ms));
	printk(KERN_INFO "[kcpuhog]: end sleep\n");

	goto restart;

term:
	*/

	printk(KERN_INFO "[kcpuhog]: Thread Stopping\n");
	do_exit(0);

	return 0;
}

static int __init init_thread(void)
{
	unsigned int cpu;

	// struct sched_param param = { .sched_priority = MAX_RT_PRIO - 1 };

	printk(KERN_INFO "[kcpuhog]: Creating Thread\n");
	cpu = 1;

	hog_thread = kthread_create(hog_thread_fn, NULL, "kcpuhog");
	// sched_setscheduler(hog_thread, SCHED_FIFO, &param);
	kthread_bind(hog_thread, cpu);
	wake_up_process(hog_thread);

	if (IS_ERR(hog_thread))
		printk(KERN_ERR "[kcpuhog]: Thread creation failed\n");
        else
		printk(KERN_INFO "[kcpuhog]: Thread Created successfully\n");

	return 0;
}

static void __exit cleanup_thread(void)
{
	printk(KERN_INFO "[kcpuhog]: Cleaning Up\n");
	kthread_stop(hog_thread);
	printk(KERN_INFO "[kcpuhog]: Thread stopped");
}

MODULE_LICENSE("GPL");
module_init(init_thread);
module_exit(cleanup_thread);
