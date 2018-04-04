#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <sched.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
#include <signal.h>


#define SCHED_POLICY	SCHED_FIFO
#define NSECPERSEC	1000000000
#define RT_PRIO		90
#define CPU_PERC	43

void cpu_hog(void)
{
	while (1)
		sqrt(rand());
}

int main(int argc, char *argv[])
{
	pid_t pid;
	int np = sysconf(_SC_NPROCESSORS_ONLN);
	struct sched_param sp;
	cpu_set_t cpu_set;
	struct timespec ts;

	switch (pid = fork()) {
	case 0:
		/* child */
		CPU_ZERO(&cpu_set);
		CPU_SET(1, &cpu_set);
		if (sched_setaffinity(0, sizeof(cpu_set_t), &cpu_set) < 0) {
			perror("sched_setaffinity");
			exit(EXIT_FAILURE);
		}

		sp.sched_priority = RT_PRIO;
		if (sched_setscheduler(0, SCHED_POLICY, &sp) < 0) {
			perror("sched_setscheduler");
			exit(EXIT_FAILURE);
		}

		printf("%d: Child is here, POLICY = %d\n", getpid(),
				sched_getscheduler(0));
		fflush(stdout);

		cpu_hog();

		break;

	case -1:
		printf("Smth went wrong\n");
		exit(EXIT_FAILURE);

	default:
		/* parent */
		sp.sched_priority = RT_PRIO + 1;
		if (sched_setscheduler(0, SCHED_POLICY, &sp) < 0) {
			perror("sched_setscheduler");
			exit(EXIT_FAILURE);
		}

		printf("%d: Parent is here, POLICY = %d\n", getpid(),
				sched_getscheduler(0));

		while (1) {
			ts.tv_sec = 0;
			ts.tv_nsec = (NSECPERSEC / 100) * CPU_PERC;
			nanosleep(&ts, NULL);
			kill(pid, SIGSTOP);

			ts.tv_sec = 0;
			ts.tv_nsec = (NSECPERSEC / 100) * (100 - CPU_PERC);
			nanosleep(&ts, NULL);
			kill(pid, SIGCONT);
		}
	}

	return 0;
}
