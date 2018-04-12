#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
#include <linux/sched/types.h>
#endif

#define MSEC_IN_SEC	1000
#define MS_TO_NS(x)	(x * 1000000)
#define MS_TO_US(x)	(x * 1000)

struct hog_thread_data {
	struct task_struct *hog_thread;
	struct hrtimer hog_hrtimer;
	int cpu;
	unsigned long work_time_ms;
	unsigned long sleep_time_ms;
	bool is_running;
};
static struct hog_thread_data hog_data[4];

// static struct task_struct *hog_thread;
// static struct hrtimer hog_hrtimer;
// static bool is_running;

// unsigned long work_time_ms = 200;
// unsigned long sleep_time_ms = 800;

enum hrtimer_restart hog_hrtimer_callback(struct hrtimer *timer)
{
	struct hog_thread_data *data = container_of(timer,
				struct hog_thread_data, hog_hrtimer);

	printk(KERN_INFO "[kcpuhog]: hrtimer_callback is called\n");

	if (data->is_running) {
		data->is_running = false;
		hrtimer_forward_now(timer,
				ktime_set(0, MS_TO_NS(data->sleep_time_ms)));
	}
	else {
		data->is_running = true;
		hrtimer_forward_now(timer,
				ktime_set(0, MS_TO_NS(data->work_time_ms)));
	}

	return HRTIMER_RESTART;
}

static int hog_threadfn(void *d)
{
	struct hog_thread_data *data = (struct hog_thread_data *)d;

	hrtimer_init(&data->hog_hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	data->hog_hrtimer.function = hog_hrtimer_callback;
	hrtimer_start(&data->hog_hrtimer, ktime_set(0,
				MS_TO_NS(data->work_time_ms)), HRTIMER_MODE_REL);

	data->is_running = true;
	while (!kthread_should_stop()) {
		printk(KERN_INFO "[kcpuhog]: begin busyloop\n");
		while (data->is_running)
			cpu_relax();
		printk(KERN_INFO "[kcpuhog]: end busyloop\n");

		printk(KERN_INFO "[kcpuhog]: begin sleep\n");
		usleep_range(MS_TO_US(data->sleep_time_ms),
				MS_TO_US(data->sleep_time_ms));
		printk(KERN_INFO "[kcpuhog]: end sleep\n");
	}

	printk(KERN_INFO "[kcpuhog]: Cancel timer\n");
	if (hrtimer_cancel(&data->hog_hrtimer))
		printk(KERN_INFO "[kcpuhog]: The timer was active\n");

	printk(KERN_INFO "[kcpuhog]: Thread Stopping\n");
	do_exit(0);

	return 0;
}

static int __init kcpuhog_init(void)
{
	printk(KERN_INFO "[kcpuhog]: Creating Thread\n");
	hog_data[0].cpu = 1;
	hog_data[0].hog_thread = kthread_create(hog_threadfn, &(hog_data[0]),
				"kcpuhog_%d", hog_data[0].cpu);
	hog_data[0].work_time_ms = 270;
	hog_data[0].sleep_time_ms = MSEC_IN_SEC - hog_data[0].work_time_ms;
	kthread_bind(hog_data[0].hog_thread, hog_data[0].cpu);
	wake_up_process(hog_data[0].hog_thread);

	if (IS_ERR(hog_data[0].hog_thread))
		printk(KERN_ERR "[kcpuhog]: Thread creation failed\n");
        else
		printk(KERN_INFO "[kcpuhog]: Thread Created successfully\n");

	return 0;
}

static void __exit kcpuhog_exit(void)
{
	printk(KERN_INFO "[kcpuhog]: Cleaning Up\n");
	kthread_stop(hog_data[0].hog_thread);
	printk(KERN_INFO "[kcpuhog]: Thread stopped");
}

MODULE_LICENSE("GPL");
module_init(kcpuhog_init);
module_exit(kcpuhog_exit);
