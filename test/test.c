#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>

int main()
{
	int i;
	struct timeval tv;

	if (gettimeofday(&tv, NULL) < 0) {
		printf("Smth went wrong\n");
		exit(1);
	}
	printf("tv_sec: %u\ntv_usec: %u\n", tv.tv_sec, tv.tv_usec);

	for (i = 1; i <= 10000000; i++)
		gettimeofday(&tv, NULL);

	return 0;
}
