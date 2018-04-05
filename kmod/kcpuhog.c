#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/sched/types.h>
#include <linux/sched.h>

#define MS_TO_NS(x) (x * 1E6L)

static struct task_struct *thread_st;
static struct hrtimer hr_timer;

const unsigned long work_time_ms = 200L;
const unsigned long sleep_time_ms = 800L;
int kthread_alive = 0;
int is_running = 0;

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

static int thread_fn(void *unused)
{
	// unsigned int cpu;
	kthread_alive = 1;
	is_running = 1;

restart:
	/*
	while (is_running && !kthread_should_stop()) {
		printk(KERN_INFO "I'M PRINTING!\n");
	}
	*/
	mdelay(work_time_ms);
	if (kthread_should_stop())
		goto term;
	usleep_range(MS_TO_NS(sleep_time_ms), MS_TO_NS(sleep_time_ms));
	goto restart;

term:
	printk(KERN_INFO "Thread Stopping\n");
	kthread_alive = 0;
	do_exit(0);

	return 0;
}

static int __init init_thread(void)
{
	unsigned int cpu;
	struct sched_param param = { .sched_priority = MAX_RT_PRIO - 1 };

	printk(KERN_INFO "Creating Thread\n");
	cpu = 1;
	param.sched_priority = 90;

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

	if (thread_st)
		printk(KERN_INFO "Thread Created successfully\n");
        else
		printk(KERN_ERR "Thread creation failed\n");

	return 0;
}

static void __exit cleanup_thread(void)
{
	// int ret;

	printk(KERN_INFO "Cleaning Up\n");

	/*
	ret = hrtimer_cancel(&hr_timer);
	if (ret)
		printk(KERN_INFO "The timer was still in use...\n");
	*/

	if (thread_st && kthread_alive)
	{
		kthread_alive = 0;
		kthread_stop(thread_st);
		printk(KERN_INFO "Thread stopped");
	}
}

MODULE_LICENSE("GPL");
module_init(init_thread);
module_exit(cleanup_thread);
