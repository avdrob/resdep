#define _GNU_SOURCE

#include <unistd.h>
#include <getopt.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <math.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <sched.h>
#include <time.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <linux/netlink.h>

#include "cpu_nl.h"

#define CLOCKID            CLOCK_MONOTONIC
#define TIMER_SIG          SIGALRM
#define CMDLINE_LENGTH     256

#define err_exit(msg)           \
        do {                    \
            perror(msg);        \
            exit(EXIT_FAILURE); \
        } while (0)

#define PCT_TO_MSEC(x)     (x * 10)
#define MSEC_TO_NSEC(x)    (x * 1e6)
#define NSEC_PER_SEC       1e9

/* The name this program was invoked by. */
char *progname;

/* Number of present CPUs. */
int cpus_onln;

/* Vector of system load values. Values are given in percentages: [0-100] */
struct sys_load {
    int st;
    int ut;
    int mem;
};

/* Generic structure containing process data */
struct proc_struct {
    pid_t pid;

    struct cpu_load cpu_load;  /* CPU load for particular process */
    volatile int is_running;   /* Flag indicating whether process is
                                * running on CPU or sleeping
                                */

    int proc_num;              /* Total number of processes */
    int ind;                   /* Index number of process */
};
static struct proc_struct proc;

/* Structures for communicating with the kernel. */
static int sock_fd;
static struct sockaddr_nl src_addr, dest_addr;
static struct nlmsghdr *nlh, *nlh_ack;
static struct nl_packet *packet;

static struct option longopts[] = {
    {"help", no_argument, NULL, 'h'},
    {"systime", required_argument, NULL, 's'},
    {"usertime", required_argument, NULL, 'u'},
    {"memory", required_argument, NULL, 'm'},

    {NULL, no_argument, NULL, 0}
};

static void usage(FILE *stream, int status)
{
    fprintf(stream,
            "Usage: %s [-s systime] [-u usertime] [-r realtime] [-m memory]\n"
            "\t[--systime=systime] [--usertime=usertime] [--memory=memory]\n"
            "\t[--help]\n\n"
            "All the values are given in percentages: [0-100]\n",
            progname);
    exit(status);
}

static int trytoconv()
{
    static int ret;
    static char *endptr;

    endptr = "";
    ret = strtol(optarg, &endptr, 10);
    if (endptr[0] != '\0' || ret < 0 || ret > 100) {
        fprintf(stderr, "%s: invalid argument: %s\nLoad value should"
                "be in range [0-100]\n", progname, optarg);
        exit (EXIT_FAILURE);
    }

    return ret;
}

static void getargs(int argc, char *argv[], struct sys_load *sys_load)
{
    int opt;

    progname = basename(argv[0]);

    if (argc < 2)
        usage(stderr, EXIT_FAILURE);

    while ((opt = getopt_long(argc, argv, "-s:u:m:h", longopts, NULL)) != -1) {
        switch (opt) {
        case 's':
            sys_load->st = trytoconv();
            break;
        case 'u':
            sys_load->ut = trytoconv();
            break;
        case 'm':
            sys_load->mem = trytoconv();
            break;
        case 'h':
            usage(stdout, EXIT_SUCCESS);
        case 1:
            fprintf(stderr, "%s: nonoption argument -- '%s'\n",
                progname, argv[optind - 1]);
            usage(stderr, EXIT_FAILURE);
        default:
            usage(stderr, EXIT_FAILURE);
        }
    }
}

static void proc_state_swith(int sig)
{
    if (proc.is_running)
        proc.is_running = 0;

    return;
}

static void cpu_proc_func(void)
{
    cpu_set_t set;
    struct sigevent sev;
    struct sigaction sa;
    timer_t timerid;
    struct itimerspec work_its;
    struct timespec    sleep_ts, delay;

    /* Make sure children are dead after parent's death */
    if (prctl(PR_SET_PDEATHSIG, SIGTERM) < 0)
        err_exit("prctl");

    /* Set process CPU afffinity */
    CPU_ZERO(&set);
    CPU_SET(proc.cpu_load.cpu_num, &set);
    if (sched_setaffinity(0, sizeof(set), &set) < 0)
        err_exit("sched_setaffinity");

    /* Establish handler for timer signal */
    sa.sa_handler = proc_state_swith;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    if (sigaction(TIMER_SIG, &sa, NULL) < 0)
        err_exit("sigaction");

    /* Create the timer */
    sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_signo = TIMER_SIG;
    if (timer_create(CLOCKID, &sev, &timerid) < 0)
        err_exit("timer_create");

    /* Set process work and sleep times */

    /* We don't want it to be periodic */
    work_its.it_interval.tv_sec = 0;
    work_its.it_interval.tv_nsec = 0;

    /* Set one-time expiration value; timer is re-armed with those
     * values by signal handler.
     */
    work_its.it_value.tv_sec = 0;
    work_its.it_value.tv_nsec = MSEC_TO_NSEC(proc.cpu_load.load_msec);

    /* Set sleep time */
    sleep_ts.tv_sec = 0;
    sleep_ts.tv_nsec = NSEC_PER_SEC - work_its.it_value.tv_nsec;

    /* Distribute timer events evenly
     *
     *  0       1/3        2/3        1s
     *  |--------*----------*---------|
     * proc1   proc2      proc3    proc1
     */

    /* 1 second alignment */
    clock_gettime(CLOCKID, &delay);
    delay.tv_sec++;
    delay.tv_nsec = 0;
    clock_nanosleep(CLOCKID, TIMER_ABSTIME, &delay, NULL);

    delay.tv_sec = 0;
    delay.tv_nsec = (NSEC_PER_SEC / proc.proc_num) * proc.ind;
    clock_nanosleep(CLOCKID, 0, &delay, NULL);

    proc.is_running = 1;
    while (1) {
        timer_settime(timerid, 0, &work_its, NULL);
        while (proc.is_running)
            sqrt(rand());
        clock_nanosleep(CLOCKID, 0, &sleep_ts, NULL);
        proc.is_running = 1;
    }
}

static void process_ack(void)
{
    if (nlh->nlmsg_seq != nlh_ack->nlmsg_seq) {
        fprintf(stderr, "Nlmsg sequence number mismatch: %d instead of "
                "%d\n", nlh_ack->nlmsg_seq, nlh->nlmsg_seq);
        exit(EXIT_FAILURE);
    }
    if (nlh_ack->nlmsg_type != NLMSG_ERROR) {
        fprintf(stderr, "Nlmsg type mismatch: should be NLMSG_ERROR\n");
        exit(EXIT_FAILURE);
    }
    else {
        struct nlmsgerr *err = (struct nlmsgerr *) NLMSG_DATA(nlh_ack);
        if (err->error != 0) {
            fprintf(stderr, "Nlmsg error == %d instead of %d\n",
                    err->error, 0);
            exit(EXIT_FAILURE);
        }
    }

    return;
}

static void send_to_kernel(int cpus_onln, const struct sys_load *sys_load)
{
    int i;

    /* Send number of threads to kernel */
    packet->packet_type = NL_THREADS_NUM;
    packet->threads_num = cpus_onln;
    nlh->nlmsg_seq = 0;
    if (sendto(sock_fd, (void *) nlh, nlh->nlmsg_len, 0, (struct sockaddr *)
               &dest_addr, sizeof(struct sockaddr_nl)) < 0)
        err_exit("sendto");

    /* Now try to receive acknowledgement */
    if (recv(sock_fd, (void *) nlh_ack,
             NLMSG_LENGTH(sizeof(struct nlmsgerr)), 0) < 0)
        err_exit("recv");
    process_ack();

    /* Send all CPU loads to kernel */
    for (i = 0; i < cpus_onln; i++) {
        packet->packet_type = NL_CPU_LOAD;
        packet->cpu_load.cpu_num = i;
        packet->cpu_load.load_msec = PCT_TO_MSEC(sys_load->st);

        nlh->nlmsg_seq++;
        if (sendto(sock_fd, (void *) nlh, nlh->nlmsg_len, 0,
                   (struct sockaddr *) &dest_addr,
                   sizeof(struct sockaddr_nl)) < 0)
            err_exit("sendto");

        if (recv(sock_fd, (void *) nlh_ack,
                 NLMSG_LENGTH(sizeof(struct nlmsgerr)), 0) < 0)
            err_exit("recv");
        process_ack();
    }
}

static void check_kmod_is_loaded(void)
{
    FILE *modf = fopen("/proc/modules", "r");
    char modname[32];

    while (!feof(modf)) {
        fscanf(modf, "%s%*[^\n]", modname);
        if (strcmp(modname, KMOD_NAME) == 0) {
            return;
        }
    }

    fprintf(stderr, "Module %s is not loaded.\nTerminating.\n", KMOD_NAME);
    exit(EXIT_FAILURE);
}

static void __attribute__((constructor)) nl_init(void)
{
    check_kmod_is_loaded();
    cpus_onln = sysconf(_SC_NPROCESSORS_ONLN);

    /* Use netlink sockets to communicate with kernel module.*/
    sock_fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_CPUHOG);
    if (sock_fd < 0)
        err_exit("socket");

    /*
     * struct sockaddr_nl {
     *     sa_family_t     nl_family;  // AF_NETLINK
     *     unsigned short  nl_pad;     // Zero
     *     pid_t           nl_pid;     // Port ID (0 for kernel)
     *     __u32           nl_groups;  // Multicast groups mask (0 for unicast)
     * };
     */
    dest_addr = (struct sockaddr_nl) { AF_NETLINK, 0, 0, 0 };
    src_addr = (struct sockaddr_nl) { AF_NETLINK, 0, getpid(), 0 };
    if (bind(sock_fd, (struct sockaddr *) &src_addr, sizeof(src_addr)) < 0)
        err_exit("bind");

    nlh = (struct nlmsghdr *) malloc(NLMSG_SPACE(sizeof(struct nl_packet)));
    nlh_ack = (struct nlmsghdr *) malloc(NLMSG_SPACE(sizeof(struct nlmsgerr)));
    if (!nlh || !nlh_ack)
        err_exit("malloc");

    nlh->nlmsg_len   = NLMSG_LENGTH(sizeof(struct nl_packet));
    nlh->nlmsg_type  = NLMSG_NOOP;
    nlh->nlmsg_flags = NLM_F_ACK;
    nlh->nlmsg_pid   = getpid();

    packet = (struct nl_packet *) NLMSG_DATA(nlh);
}

static void __attribute__((destructor)) nl_fini(void)
{
    /* Tell kernel module to stop kthreads. */
    packet->packet_type = NL_STOP_THREADS;
    nlh->nlmsg_seq++;
    if (sendto(sock_fd, (void *) nlh, nlh->nlmsg_len, 0, (struct sockaddr *)
               &dest_addr, sizeof(struct sockaddr_nl)) < 0)
        err_exit("sendto");
    if (recv(sock_fd, (void *) nlh_ack,
             NLMSG_LENGTH(sizeof(struct nlmsgerr)), 0) < 0)
        err_exit("recv");

    free(nlh);
    free(nlh_ack);
    close(sock_fd);
}

int main(int argc, char *argv[])
{
    int i;
    int proc_num;
    struct sys_load sys_load = {0};

    getargs(argc, argv, &sys_load);
    proc_num = cpus_onln;
    send_to_kernel(cpus_onln, &sys_load);

    for (i = 0; i < proc_num; i++) {
        proc.proc_num = proc_num;
        proc.ind = i;
        proc.cpu_load.cpu_num = i;
        proc.cpu_load.load_msec = PCT_TO_MSEC(sys_load.ut);

        proc.pid = fork();
        if (!proc.pid)
            cpu_proc_func();
    }

    for (i = 0; i < proc_num; i++)
        wait(NULL);

    return 0;
}
