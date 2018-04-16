#include <unistd.h>
#include <getopt.h>
#include <stdlib.h>
#include <stdio.h>
#include <libgen.h>

/* The name this program was invoked by. */
char *progname;

/* Vector of system load values. Values are given in percentages: [0-100] */
struct load {
	int st;
	int ut;
	int mem;
};
static struct load load = {0};

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
			load.st = trytoconv();
			break;
		case 'u':
			load.ut = trytoconv();
			break;
		case 'm':
			load.mem = trytoconv();
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

int main(int argc, char *argv[])
{
	/*
	 * Fill some static struct with option values
	 */
	getargs(argc, argv);

	return 0;
}
