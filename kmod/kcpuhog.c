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

static struct task_struct *thread_st;

const unsigned long work_time_ms = 200L;
const unsigned long sleep_time_ms = 800L;

/*
enum hrtimer_restart my_hrtimer_callback(struct hrtimer *timer )
{
	printk(KERN_INFO "my_hrtimer_callback is called\n");

	if (is_running) {
		is_running = 0;
		hrtimer_forward_now(&hr_timer,
				ktime_set(0, MS_TO_NS(sleep_time_ms)));
	}
	else {
		is_running = 1;
		hrtimer_forward_now(&hr_timer,
				ktime_set(0, MS_TO_NS(work_time_ms)));
	}


	return kthread_alive ? HRTIMER_RESTART : HRTIMER_NORESTART;
}
*/

static int thread_fn(void *unused)
{
	unsigned long start_jiffies, tmo;

restart:
	/*
	while (is_running && !kthread_should_stop()) {
		printk(KERN_INFO "I'M PRINTING!\n");
	}
	*/

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
	printk(KERN_INFO "[kcpuhog]: Thread Stopping\n");
	do_exit(0);

	return 0;
}

static int __init init_thread(void)
{
	unsigned int cpu;
	struct sched_param param = { .sched_priority = MAX_RT_PRIO - 1 };

	printk(KERN_INFO "[kcpuhog]: Creating Thread\n");
	cpu = 1;

	/*
	hrtimer_init(&hr_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	hr_timer.function = my_hrtimer_callback;
	hrtimer_start(&hr_timer, ktime_set(0, MS_TO_NS(work_time_ms)),
			HRTIMER_MODE_REL);
	*/

	thread_st = kthread_create(thread_fn, NULL, "kcpuhog");
	sched_setscheduler(thread_st, SCHED_FIFO, &param);
	kthread_bind(thread_st, cpu);
	wake_up_process(thread_st);

	if (IS_ERR(thread_st))
		printk(KERN_ERR "[kcpuhog]: Thread creation failed\n");
        else
		printk(KERN_INFO "[kcpuhog]: Thread Created successfully\n");

	return 0;
}

static void __exit cleanup_thread(void)
{
	// int ret;

	printk(KERN_INFO "[kcpuhog]: Cleaning Up\n");

	/*
	ret = hrtimer_cancel(&hr_timer);
	if (ret)
		printk(KERN_INFO "The timer was still in use...\n");
	*/

	if (thread_st)
	{
		kthread_stop(thread_st);
		printk(KERN_INFO "[kcpuhog]: Thread stopped");
	}
}

MODULE_LICENSE("GPL");
module_init(init_thread);
module_exit(cleanup_thread);
