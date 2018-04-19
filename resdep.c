#define _GNU_SOURCE

#include <unistd.h>
#include <getopt.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <libgen.h>
#include <math.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <sched.h>
#include <signal.h>
#include <time.h>

#define CLOCKID			CLOCK_MONOTONIC
#define TIMER_SIG		SIGALRM

/* This is a workaroud (See https://lkml.org/lkml/2010/10/17/181) */
#define sigev_notify_thread_id	_sigev_un._tid

#define err_exit(msg)		do { \
					perror(msg); \
					exit(EXIT_FAILURE); \
	                        } while (0)

#define PCT_TO_MSEC(x)		(x * 10)
#define MSEC_TO_NSEC(x)		(x * 1e6)
#define NSEC_PER_SEC		1e9

/* The name this program was invoked by. */
char *progname;

/* Vector of system load values. Values are given in percentages: [0-100] */
struct sys_load {
	int st;
	int ut;
	int mem;
};
static struct sys_load sys_load = {0};

/* CPU load argument for both user & kernel threads */
struct cpu_load {
	int cpu_num;
	int load_msec;
};

/* Generic structure containing thread data */
struct thread_struct {
	pthread_t ptid;			/* POSIX thread ID */
	pid_t tid;			/* Kernel's thread ID */

	struct cpu_load cpu_load;
	int is_running;

	int thr_num;			/* Total number of threads */
	int ind;			/* Index number of thread */

	timer_t timerid;
	struct itimerspec work_ts;
	struct itimerspec sleep_ts;
};
static struct thread_struct *thr;

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

static inline int trytoconv()
{
	static int ret;
	static char *endptr;

	endptr = "";
	ret = strtol(optarg, &endptr, 10);
	if (endptr[0] != '\0' || ret < 0 || ret > 100) {
		fprintf(stderr, "%s: invalid argument: %s\nLoad value should be in"
				" range [0-100]\n", progname, optarg);
		exit (EXIT_FAILURE);
	}

	return ret;
}

static void getargs(int argc, char *argv[])
{
	int opt;

	progname = basename(argv[0]);

	if (argc < 2)
		usage(stderr, EXIT_FAILURE);

	while ((opt = getopt_long(argc, argv, "-s:u:m:h", longopts, NULL))
		!= -1) {
		switch (opt) {
		case 's':
			sys_load.st = trytoconv();
			break;
		case 'u':
			sys_load.ut = trytoconv();
			break;
		case 'm':
			sys_load.mem = trytoconv();
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

static void thr_state_swith(int sig, siginfo_t *info, void *ucontext)
{
	struct thread_struct *data = (struct thread_struct *)
						info->si_value.sival_ptr;
	if (data->is_running)
		data->is_running = 0;

	return;
}

static void print_ts(void)
{
	struct timespec ts;

	clock_gettime(CLOCKID, &ts);
	printf("[%ld.%ld] ", ts.tv_sec, ts.tv_nsec / 1000);

	return;
}

static void *cpu_thr_func(void *arg)
{
	cpu_set_t set;
	struct thread_struct *data;
	struct sigevent sev;
	struct sigaction sa;
	struct timespec delay;

	data = (struct thread_struct *)arg;
	data->tid = syscall(__NR_gettid);

	/* Set thread afffinity */
	CPU_ZERO(&set);
	CPU_SET(data->cpu_load.cpu_num, &set);
	if (sched_setaffinity(0, sizeof(set), &set) < 0)
		err_exit("sched_setaffinity");

	/* int i;
	if (sched_getaffinity(0, sizeof(set), &set) < 0)
		err_exit("sched_getaffinity");
	for (i = 0; i < sysconf(_SC_NPROCESSORS_ONLN); i++) {
		if (CPU_ISSET(i, &set)) {
			print_ts();
			printf("thread %d: CPU %d\n", data->tid, i);
			fflush(stdout);
		}
	} */

	/* Establish handler for timer signal */
	sa.sa_sigaction = thr_state_swith;
	sa.sa_flags = SA_SIGINFO;
	sigemptyset(&sa.sa_mask);
	if (sigaction(TIMER_SIG, &sa, NULL) < 0)
		err_exit("sigaction");

	/* Create the timer */
	sev.sigev_notify = SIGEV_THREAD_ID;
	sev.sigev_signo = TIMER_SIG;
	sev.sigev_notify_thread_id = data->tid;
	sev.sigev_value.sival_ptr = (void *) data;
	if (timer_create(CLOCKID, &sev, &(data->timerid)) < 0)
		err_exit("timer_create");

	/* Set thread's work and sleep times */

	/* We don't want it to be periodic */
	data->work_ts.it_interval.tv_sec = 0;
	data->work_ts.it_interval.tv_nsec = 0;
	data->sleep_ts.it_interval.tv_sec = 0;
	data->sleep_ts.it_interval.tv_nsec = 0;

	/* Set one-time expiration values; timer is re-armed with those
	 * values by signal handler.
	 */
	data->work_ts.it_value.tv_sec = 0;
	data->work_ts.it_value.tv_nsec = MSEC_TO_NSEC(data->cpu_load.load_msec);
	data->sleep_ts.it_value.tv_sec = 0;
	data->sleep_ts.it_value.tv_nsec = NSEC_PER_SEC -
						data->work_ts.it_value.tv_nsec;

	/* TODO: sleep for dome time here... */
	delay.tv_sec = 0;
	delay.tv_nsec = (NSEC_PER_SEC / data->thr_num) * data->ind;
	// clock_nanosleep(CLOCKID, 0, &delay, NULL);

	data->is_running = 1;
	while (1) {
		/* print_ts();
		printf("thread %d: begin work\n", data->tid);
		fflush(stdout); */
		timer_settime(data->timerid, 0, &(data->work_ts), NULL);
		while (data->is_running)
			sqrt(rand());
		/* print_ts();
		printf("thread %d: end work\n", data->tid);
		print_ts();
		printf("thread %d: begin sleep\n", data->tid);
		fflush(stdout); */
		clock_nanosleep(CLOCKID, 0, &(data->sleep_ts.it_value), NULL);
		data->is_running = 1;
		/* print_ts();
		printf("thread %d: end sleep\n", data->tid);
		fflush(stdout); */
	}
}

int main(int argc, char *argv[])
{
	int i;
	int cpus_onln;
	int thr_num;

	/* Fill some static struct with option values */
	getargs(argc, argv);

	cpus_onln = sysconf(_SC_NPROCESSORS_ONLN);
	thr_num = 2;
	thr = (struct thread_struct *) calloc(cpus_onln,
			sizeof(struct thread_struct));

	for (i = 0; i < thr_num; i++) {
		if (pthread_create(&(thr[i].ptid), NULL, cpu_thr_func,
					&(thr[i])))
			err_exit("pthread_create");

		thr[i].thr_num = thr_num;
		thr[i].ind = i;
		thr[i].cpu_load.cpu_num = i;
		thr[i].cpu_load.load_msec = PCT_TO_MSEC(sys_load.ut);
	}

	for (i = 0; i < thr_num; i++)
		if (pthread_join(thr[i].ptid, NULL))
			err_exit("pthread_join");
	free(thr);

	return 0;
}
