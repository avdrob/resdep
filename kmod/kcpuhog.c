#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 11, 0)
#include <linux/sched.h>
#else
#include <linux/sched/signal.h>
#endif

static struct task_struct *thread_st;
int is_running = 0;

static int thread_fn(void *unused)
{
	unsigned int cpu;

	allow_signal(SIGKILL);
	allow_signal(SIGSTOP);
	allow_signal(SIGCONT);

	is_running = 1;

	while (!kthread_should_stop()) {
		cpu = get_cpu();
		put_cpu();
		printk(KERN_INFO "kcpuhog: cpu = %d\n", cpu);
		if (signal_pending(thread_st)) {
			break;
		}
		msleep(700);
        }

	printk(KERN_INFO "Thread Stopping\n");
	is_running = 0;
	do_exit(0);

	return 0;
}

static int __init init_thread(void)
{
	unsigned int cpu;

	printk(KERN_INFO "Creating Thread\n");
	cpu = 1;
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
	printk(KERN_INFO "Cleaning Up\n");
	if (thread_st && is_running)
	{
		kthread_stop(thread_st);
		printk(KERN_INFO "Thread stopped");
	}
}

MODULE_LICENSE("GPL");
module_init(init_thread);
module_exit(cleanup_thread);
