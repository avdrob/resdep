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

#include "loadgen_netlink.h"

#define MSEC_IN_SEC    1000
#define MS_TO_NS(x)    (x * 1000000)
#define MS_TO_US(x)    (x * 1000)

struct hog_thread_data {
    bool cpu_active;
    struct task_struct *hog_thread;
    struct hrtimer hog_hrtimer;
    unsigned long work_time_ms;
    unsigned long sleep_time_ms;
    bool is_running;
};
static struct hog_thread_data *hog_data;

static struct sock *nl_sk = NULL;
static unsigned int num_cpus = 0;

static enum hrtimer_restart hog_hrtimer_callback(struct hrtimer *timer)
{
    struct hog_thread_data *data = container_of(timer,
                                                struct hog_thread_data,
                                                hog_hrtimer);

    if (data->is_running) {
        data->is_running = false;
        hrtimer_forward_now(timer, ktime_set(0, MS_TO_NS(data->sleep_time_ms)));
    }
    else {
        data->is_running = true;
        hrtimer_forward_now(timer, ktime_set(0, MS_TO_NS(data->work_time_ms)));
    }

    return HRTIMER_RESTART;
}

static int hog_threadfn(void *d)
{
    struct hog_thread_data *data = (struct hog_thread_data *)d;

    hrtimer_init(&data->hog_hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    data->hog_hrtimer.function = hog_hrtimer_callback;
    hrtimer_start(&data->hog_hrtimer,
                  ktime_set(0, MS_TO_NS(data->work_time_ms)),
                  HRTIMER_MODE_REL);

    data->is_running = true;
    while (!kthread_should_stop()) {
        while (data->is_running)
            cpu_relax();

        usleep_range(MS_TO_US(data->sleep_time_ms),
                     MS_TO_US(data->sleep_time_ms));
    }

    printk(KERN_INFO "[%s]: Cancel timer\n", KMOD_NAME);
    if (hrtimer_cancel(&data->hog_hrtimer))
        printk(KERN_INFO "[%s]: The timer was active\n", KMOD_NAME);

    printk(KERN_INFO "[%s]: %s stopping\n", KMOD_NAME, data->hog_thread->comm);
    do_exit(0);
}

static void nl_send_ack(const struct nlmsghdr *nlh)
{
    struct sk_buff *skb_out;
    struct nlmsgerr err;

    /* Here we are sending an acknowledgement back to userspace */
    skb_out = nlmsg_new(sizeof(struct nlmsgerr), 0);
    if (!skb_out) {
        printk(KERN_ERR "[%s]: Failed to allocate new skb\n", KMOD_NAME);
        return;
    }

    memset((void *) &err, 0, sizeof(struct nlmsgerr));
    err.error = 0;        /* 0 for acknowledgement */
    err.msg = *nlh;       /* header causing acknowledgment response */

    /* Now nlh points to header of a new netlink message put to buffer */
    nlh = nlmsg_put(skb_out, 0, err.msg.nlmsg_seq, NLMSG_ERROR,
                    sizeof(struct nlmsgerr), 0);
    NETLINK_CB(skb_out).dst_group = 0;    /* not in mcast group */
    memcpy(nlmsg_data(nlh), (void *) &err, sizeof(struct nlmsgerr));

    if (nlmsg_unicast(nl_sk, skb_out, err.msg.nlmsg_pid) < 0)
        printk(KERN_INFO "[%s]: Error while sending back to user\n", KMOD_NAME);
}

static void kloadgend_run_threads(void)
{
    int i;

    printk(KERN_INFO "[%s]: Running kthreads\n", KMOD_NAME);
    for (i = 0; i < num_cpus; ++i) {
        if (!hog_data[i].cpu_active)
            continue;

        hog_data[i].hog_thread = kthread_create(hog_threadfn,
                                                &(hog_data[i]),
                                                "%s%d", KMOD_NAME, i);
        if (IS_ERR(hog_data[i].hog_thread))
            printk(KERN_ERR "[%s]: Thread creation failed\n", KMOD_NAME);
        else {
            kthread_bind(hog_data[i].hog_thread, i);
            wake_up_process(hog_data[i].hog_thread);
        }
    }
}

static void kloadgend_stop_threads(void)
{
    int i;
    int cnt = 0;

    if (!hog_data)
        return;

    for (i = 0; i < num_cpus; i++) {
        if (hog_data[i].cpu_active &&
                hog_data[i].hog_thread &&
                !IS_ERR(hog_data[i].hog_thread)) {
            kthread_stop(hog_data[i].hog_thread);
            hog_data[i].hog_thread = NULL;
            hog_data[i].cpu_active = false;
            cnt++;
        }
    }

    if (cnt)
        printk(KERN_INFO "[%s]: Kthreads are terminated\n", KMOD_NAME);
}

static bool nl_check_pid_and_seq(pid_t pid, pid_t prev_pid,
                                 int seq, int prev_seq)
{
    if (pid != prev_pid) {
        printk(KERN_ERR "[%s]: nlmsg pid mismatch: "
               "should be %d, but received %d\n",
               KMOD_NAME, prev_pid, pid);
        return false;
    }
    if (seq != prev_seq + 1) {
        printk(KERN_ERR "[%s]: nlmsg sequence number mismatch: "
               "should be %d, but received %d\n",
               KMOD_NAME, prev_seq + 1, seq);
        return false;
    }

    return true;
}

static inline void reset_hog_data(void)
{
    memset((void *) hog_data, 0, sizeof(struct hog_thread_data) * num_cpus);
}

static void nl_recv_msg(struct sk_buff *skb)
{
    struct nlmsghdr *nlh;
    struct loadgen_packet_nl *nl_packet;
    unsigned int cpu_num, load_msec;

    static pid_t pid = 0;
    static int seq = -1;

    nlh = (struct nlmsghdr *) skb->data;
    nl_packet = (struct loadgen_packet_nl *) nlmsg_data(nlh);

    switch (nl_packet->packet_type) {
    case NL_INIT:
        // if (nlh->nlmsg_seq != 0) {
        //     printk(KERN_ERR "[%s]: nlmsg sequence number "
        //            "mismatch: should be 0, but received %d\n",
        //            KMOD_NAME, nlh->nlmsg_seq);
        //     return;
        // }
        reset_hog_data();
        break;

    case NL_CPU_LOAD:
        /* Here we receive CPU loads */
        // if (!nl_check_pid_and_seq(nlh->nlmsg_pid, pid,
        //                           nlh->nlmsg_seq, seq))
        //     return;

        cpu_num = (nl_packet->cpu_load).cpu_num;
        load_msec = (nl_packet->cpu_load).load_msec;

        if (cpu_num >= num_cpus) {
            printk(KERN_ERR "[%s]: CPU number %u is too large\n",
                   KMOD_NAME, cpu_num);
            return;
        }
        if (load_msec > MSEC_IN_SEC) {
            printk(KERN_ERR "[%s]: load of %u msec is too large\n",
                   KMOD_NAME, load_msec);
            return;
        }

        hog_data[cpu_num].work_time_ms = load_msec;
        hog_data[cpu_num].sleep_time_ms =
            MSEC_IN_SEC - hog_data[cpu_num].work_time_ms;
        hog_data[cpu_num].cpu_active = true;

        break;

    case NL_RUN:
        /* Run all threads at once. */
        // if (!nl_check_pid_and_seq(nlh->nlmsg_pid, pid,
        //                           nlh->nlmsg_seq, seq))
        //     return;
        kloadgend_run_threads();
        break;

    case NL_STOP:
        // if (!nl_check_pid_and_seq(nlh->nlmsg_pid, pid,
        //                           nlh->nlmsg_seq, seq))
        //     return;
        kloadgend_stop_threads();
        reset_hog_data();
        break;

    default:
        unreachable();
        break;
    }

    seq = nlh->nlmsg_seq;
    pid = nlh->nlmsg_pid;
    nl_send_ack(nlh);

    return;
}

static int __init kloadgend_init(void)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0)
    struct netlink_kernel_cfg cfg = {
        .input = nl_recv_msg,
    };
    nl_sk = netlink_kernel_create(&init_net, NETLINK_LOADGEN, &cfg);
#else
    nl_sk = netlink_kernel_create(&init_net, NETLINK_LOADGEN, 0,
                                  nl_recv_msg, NULL, THIS_MODULE);
#endif

    printk(KERN_INFO "[%s]: Initializing module\n", KMOD_NAME);

    if (!nl_sk) {
        printk(KERN_ERR "[%s]: Error creating socket\n", KMOD_NAME);
        return -1;
    }

    num_cpus = num_online_cpus();
    hog_data = kmalloc(sizeof(struct hog_thread_data) * num_cpus, GFP_KERNEL);
    if (!hog_data) {
        printk(KERN_ERR "[%s]: kmalloc failed\n", KMOD_NAME);
        return -1;
    }
    reset_hog_data();

    return 0;
}

static void __exit kloadgend_exit(void)
{
    printk(KERN_INFO "[%s]: Cleaning Up\n", KMOD_NAME);
    netlink_kernel_release(nl_sk);
    kloadgend_stop_threads();
    kfree(hog_data);
}

MODULE_LICENSE("GPL");
module_init(kloadgend_init);
module_exit(kloadgend_exit);
