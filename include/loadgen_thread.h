#ifndef LOADGEN_THREAD_H
#define LOADGEN_THREAD_H

#include <pthread.h>
#include <signal.h>

#define MSEC_TO_NSEC(x)             (x * 1e6)
#define NSEC_PER_SEC                1e9

#define LOADGEN_THREAD_TERM_SIGNAL  SIGUSR1
#define LOADGEN_THREAD_CLOCKID      CLOCK_MONOTONIC

/* Per-thread data. */
extern pthread_t *loadgen_threads;
extern int *is_running;

/* Total number of threads running. */
extern int loadgen_threads_num;

/* Number of running CPU threads. */
extern int loadgen_cpu_threads_num;

/* Use this to run threads in detached state. */
pthread_attr_t pthread_attr;

extern void loadgen_thread_init(void);
extern void *loadgen_cpu_thread_fn(void *arg);
extern void thread_sigterm_handler(int signo);
extern void loadgen_run_threads(void);
extern void loadgen_kill_threads(void);

#endif // LOADGEN_THREAD_H
