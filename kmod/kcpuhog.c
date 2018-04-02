#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/version.h>
#include <linux/hrtimer.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 11, 0)
#include <linux/sched.h>
#else
#include <linux/sched/signal.h>
#endif

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
	hrtimer_forward_now(&hr_timer, ktime_set(0, MS_TO_NS(sleep_time_ms)));

	return kthread_alive ? HRTIMER_RESTART : HRTIMER_NORESTART;
}

static int thread_fn(void *unused)
{
	unsigned int cpu;

	allow_signal(SIGKILL);
	allow_signal(SIGSTOP);
	allow_signal(SIGCONT);

	kthread_alive = 1;
	is_running = 1;

	while (is_running && !kthread_should_stop()) {
		cpu = get_cpu();
		put_cpu();
		printk(KERN_INFO "kcpuhog: cpu = %d\n", cpu);
		if (signal_pending(thread_st)) {
			break;
		}
        }


	printk(KERN_INFO "Thread Stopping\n");
	kthread_alive = 0;
	do_exit(0);

	return 0;
}

static int __init init_thread(void)
{
	unsigned int cpu;

	printk(KERN_INFO "Creating Thread\n");
	cpu = 1;

	hrtimer_init(&hr_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	hr_timer.function = my_hrtimer_callback;
	hrtimer_start(&hr_timer, ktime_set(0, MS_TO_NS(work_time_ms)),
			HRTIMER_MODE_REL);

	thread_st = kthread_create(thread_fn, NULL, "kcpuhog");
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
	int ret;

	printk(KERN_INFO "Cleaning Up\n");

	ret = hrtimer_cancel(&hr_timer);
	if (ret)
		printk(KERN_INFO "The timer was still in use...\n");

	if (thread_st && kthread_alive)
	{
		kthread_stop(thread_st);
		printk(KERN_INFO "Thread stopped");
	}
}

MODULE_LICENSE("GPL");
module_init(init_thread);
module_exit(cleanup_thread);
