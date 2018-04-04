#define  _GNU_SOURCE

#include <unistd.h>
#include <sys/syscall.h>
#include <sched.h>
#include <stdlib.h>
#include <stdio.h>

int main()
{
	int i;
	int pipefd[2];
	cpu_set_t cpu_set;
	char buf;
	struct sched_param sp = {
		.sched_priority = 90
	};

	CPU_ZERO(&cpu_set);
	CPU_SET(3, &cpu_set);
	if (sched_setaffinity(0, sizeof(cpu_set_t), &cpu_set) < 0) {
		perror("sched_setaffinity");
		exit(EXIT_FAILURE);
	}

	if (sched_setscheduler(0, SCHED_FIFO, &sp) < 0) {
		perror("sched_setscheduler");
		exit(EXIT_FAILURE);
	}

	// for (i = 1; i <= 10000000; i++)
	if (pipe(pipefd) < 0) {
		perror("pipe");
		exit(EXIT_FAILURE);
	}

	buf = '\n';
	while (1) {
		// syscall(SYS_getpid);
		write(pipefd[1], &buf , 1);
		read(pipefd[0], &buf, 1);
	}

	return 0;
}
