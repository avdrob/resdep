#ifndef CPU_NL_H
#define CPU_NL_H

#define NETLINK_CPUHOG		31

/* CPU load argument for both user processes & kernel threads */
struct cpu_load {
	unsigned int cpu_num;
	unsigned int load_msec;
};

#endif	/* CPU_NL_H */
