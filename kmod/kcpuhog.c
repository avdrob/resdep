#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/version.h>
#include <linux/cpumask.h>
#include <linux/slab.h>
#include <net/sock.h>
#include <linux/netlink.h>
#include <linux/skbuff.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
#include <linux/sched/types.h>
#endif

#include "cpu_nl.h"

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
static struct hog_thread_data *hog_data;

static struct sock *nl_sk = NULL;
static unsigned int msg_rcvd = 0;	/* Number of received messages */
static bool rcv_loads = false;		/* Flag is risen when receiving actual
					   CPU loads */

static struct cpu_load *cpu_load;
static unsigned int num_cpus = 0;
static unsigned int num_threads = 0;
static unsigned int cpus_bitmask = 0;

enum hrtimer_restart hog_hrtimer_callback(struct hrtimer *timer)
{
	struct hog_thread_data *data = container_of(timer,
				struct hog_thread_data, hog_hrtimer);

	// printk(KERN_INFO "[kcpuhog]: hrtimer_callback is called\n");

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
		// printk(KERN_INFO "[kcpuhog]: begin busyloop\n");
		while (data->is_running)
			cpu_relax();
		// printk(KERN_INFO "[kcpuhog]: end busyloop\n");

		// printk(KERN_INFO "[kcpuhog]: begin sleep\n");
		usleep_range(MS_TO_US(data->sleep_time_ms),
				MS_TO_US(data->sleep_time_ms));
		// printk(KERN_INFO "[kcpuhog]: end sleep\n");
	}

	printk(KERN_INFO "[kcpuhog]: Cancel timer\n");
	if (hrtimer_cancel(&data->hog_hrtimer))
		printk(KERN_INFO "[kcpuhog]: The timer was active\n");

	printk(KERN_INFO "[kcpuhog]: Thread %d Stopping\n", data->cpu);
	do_exit(0);
}

static void nl_recv_msg(struct sk_buff *skb)
{
	struct nlmsghdr *nlh;
	struct sk_buff *skb_out;
	struct nlmsgerr err;
	int pid, seq;

	nlh = (struct nlmsghdr *) skb->data;
	pid = nlh->nlmsg_pid;
	seq = nlh->nlmsg_seq;

	if (rcv_loads == false) { /* Initial receiving number of threads */
		if (seq != 0)
			printk(KERN_ALERT "[kcpuhog]: nlmsg sequence number "
					"mismatch: should be 0\n");

		num_threads = *((int *) nlmsg_data(nlh));
		if (num_threads > num_cpus) {
			printk(KERN_ERR "[kcpuhog]: threads number %d is too "
					"large\n", num_threads);
			return;
		}
		printk(KERN_INFO "[kcpuhog]: nl message %d; seq == %d\n",
					num_threads, seq);

		/* From now on we're going to receive CPU loads */
		rcv_loads = true;

		hog_data = kmalloc(sizeof(struct hog_thread_data) * num_threads,
					GFP_KERNEL);
		if (!hog_data) {
			printk(KERN_ERR "[kcpuhog]: kmalloc failed\n");
			goto err_exit;
		}
		cpu_load = kmalloc(sizeof(struct cpu_load), GFP_KERNEL);
		if (!cpu_load) {
			printk(KERN_ERR "[kcpuhog]: kmalloc failed\n");
			goto free_hog_data;
		}

		/* Everything is ok */
		memset((void *) hog_data, 0, sizeof(struct hog_thread_data) *
						num_threads);
		memset((void *) cpu_load, 0, sizeof(struct cpu_load));
	}
	else {	/* Here we receive CPU loads */
		unsigned int cpu_num, load_msec;

		if (seq != msg_rcvd + 1)
			printk(KERN_ALERT "[kcpuhog]: nlmsg sequence number "
					"mismatch: should be %d\n", msg_rcvd + 1);

		cpu_load = (struct cpu_load *) nlmsg_data(nlh);
		cpu_num = cpu_load->cpu_num;
		load_msec = cpu_load->load_msec;

		if (cpu_num >= num_cpus) {
			printk(KERN_ERR "[kcpuhog]: CPU number %u is too "
					"large\n", cpu_num);
			return;
		}
		if (cpus_bitmask & (0x1 << cpu_num)) {
			printk(KERN_ERR "[kcpuhog]: CPU number %u is already "
					"reserved\n", cpu_num);
			return;
		}
		if (load_msec > MSEC_IN_SEC) {
			printk(KERN_ERR "[kcpuhog]: load of %u msec is too "
					"large\n", load_msec);
			return;
		}
		cpus_bitmask |= 0x1 << cpu_num;
		printk(KERN_INFO "[kcpuhog]: seq == %d, cpu_num == %u, "
					"load_msec == %u\n", seq, cpu_num,
					load_msec);

		/* Now it's time to spawn corresponding kthread */
		hog_data[msg_rcvd].work_time_ms = load_msec;
		hog_data[msg_rcvd].sleep_time_ms = MSEC_IN_SEC -
					hog_data[msg_rcvd].work_time_ms;
		hog_data[msg_rcvd].cpu = cpu_num;

		hog_data[msg_rcvd].hog_thread = kthread_create(hog_threadfn,
					&(hog_data[msg_rcvd]),
					"kcpuhog_%d", cpu_num);
		if (IS_ERR(hog_data[msg_rcvd].hog_thread))
			printk(KERN_ERR "[kcpuhog]: Thread creation failed\n");
		else {
			kthread_bind(hog_data[msg_rcvd].hog_thread,
						hog_data[msg_rcvd].cpu);
			wake_up_process(hog_data[msg_rcvd].hog_thread);
		}

		msg_rcvd++;
	}

	/* Here we are sending an acknowledgement back to userspace */
	skb_out = nlmsg_new(sizeof(struct nlmsgerr), 0);
	if (!skb_out) {
		printk(KERN_ERR "[kcpuhog]: Failed to allocate new skb\n");
		return;
	}

	memset((void *) &err, 0, sizeof(struct nlmsgerr));
	err.error = 0;		/* 0 for acknowledgement */
	err.msg = *nlh;		/* header causing acknowledgment response */

	/* Now nlh points to header of a new netlink message put to buffer */
	nlh = nlmsg_put(skb_out, 0, seq, NLMSG_ERROR, sizeof(struct nlmsgerr), 0);
	NETLINK_CB(skb_out).dst_group = 0;	/* not in mcast group */
	memcpy(nlmsg_data(nlh), (void *) &err, sizeof(struct nlmsgerr));

	if (nlmsg_unicast(nl_sk, skb_out, pid) < 0)
		printk(KERN_INFO "[kcpuhog]: Error while sending back to user\n");

	if (msg_rcvd == num_threads) { /* Socket is no longer needed */
		netlink_kernel_release(nl_sk);
		nl_sk = NULL;	/* For safe double release */
	}

	return;

free_hog_data:
	kfree(hog_data);
	hog_data = NULL;	/* For safe double kfree */
err_exit:
	return;
}

static int __init kcpuhog_init(void)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0)
	struct netlink_kernel_cfg cfg = {
		.input = nl_recv_msg,
	};
	nl_sk = netlink_kernel_create(&init_net, NETLINK_CPUHOG, &cfg);
#else
	nl_sk = netlink_kernel_create(&init_net, NETLINK_CPUHOG, 0, nl_recv_msg,
				NULL, THIS_MODULE);
#endif

	printk(KERN_INFO "[kcpuhog]: Initializing module\n");

	if (!nl_sk) {
		printk(KERN_ALERT "[kcpuhog]: Error creating socket\n");
		return -1;
	}

	num_cpus = num_online_cpus();

	return 0;
}

static void __exit kcpuhog_exit(void)
{
	int i;

	printk(KERN_INFO "[kcpuhog]: Cleaning Up\n");

	netlink_kernel_release(nl_sk);

	for (i = 0; i < num_threads; i++)
		if (hog_data[i].hog_thread && !IS_ERR(hog_data[i].hog_thread))
			kthread_stop(hog_data[i].hog_thread);

	/* Passing possible NULL to kfree is legal */
	kfree(cpu_load);
	kfree(hog_data);
}

MODULE_LICENSE("GPL");
module_init(kcpuhog_init);
module_exit(kcpuhog_exit);
