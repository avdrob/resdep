#ifndef LOADGEN_SYSLOAD_H
#define LOADGEN_SYSLOAD_H

#include <sys/mman.h>
#include "loadgen_cpuload.h"
#include "loadgen_log.h"

/* Number of present CPUs. */
extern int cpus_onln;

/* Size of page frame, in bytes. */
extern int page_size;

/* Number of present physical pages. */
extern int phys_pages;

/* Memory allocated by daemon (via anonymous mmap).*/
extern unsigned char *loadgen_mem;

/* Pool of various cpu_loads. */
struct cpu_load *cpu_loads;

struct loadgen_sysload {
    struct cpu_load *cpu_load_user;
    struct cpu_load *cpu_load_kernel;
    int mem_pages_num;
};

/* System loads: current one and the one we will produce next.*/
extern struct loadgen_sysload *cur_sysload, *new_sysload;

/* Memory chunk allocated by daemon. */
extern unsigned char *loadgen_mem;

/* Block device name. */
extern char devname[];

extern void loadgen_sysload_init(void);
extern void swap_sysloads(void);
extern void reset_sysload(struct loadgen_sysload *sysload);
extern int loadgen_sysload_empty(struct loadgen_sysload *sysload);
extern int loadgen_cpuload_empty(struct cpu_load *cpu_load);
extern void deallocate_memory(void);
extern void allocate_memory(void);
extern void get_block_devname();

static inline int roundff(float num)
{
    return num > 0 ? num + 0.5 : num - 0.5;
}

static inline int percent_to_msec(float perc)
{
    return roundff(perc * 10);
}

static inline int percent_to_pages_num(float perc)
{
    return roundff(phys_pages * (perc / 100.0));
}

#endif // LOADGEN_SYSLOAD_H
