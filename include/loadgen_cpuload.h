#ifndef LOADGEN_CPULOAD_H
#define LOADGEN_CPULOAD_H

/* Struct used by both user and kernel daemons. */
struct cpu_load {
    int cpu_num;
    int load_msec;
};

#endif // LOADGEN_CPULOAD_H
