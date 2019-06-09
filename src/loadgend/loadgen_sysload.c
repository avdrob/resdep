#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#include "loadgen_sysload.h"
#include "loadgen_log.h"

int cpus_onln = -1;
int page_size = -1;
int phys_pages = -1;

static struct loadgen_sysload loadgen_sysload[2] = {
                                  {NULL, NULL, 0},
                                  {NULL, NULL, 0}
                              };

struct loadgen_sysload *cur_sysload = &(loadgen_sysload[0]);
struct loadgen_sysload *new_sysload = &(loadgen_sysload[1]);
unsigned char *loadgen_mem = NULL;
struct cpu_load *cpu_loads = NULL;
char devname[64] = "/dev/";

void loadgen_sysload_init(void)
{
    /* Get system config values. */
    cpus_onln = sysconf(_SC_NPROCESSORS_ONLN);
    page_size = sysconf(_SC_PAGE_SIZE);
    phys_pages = sysconf(_SC_PHYS_PAGES);

    /* Get block device. */
    get_block_devname();

    /* Init loadgen_sysload fields. */
    cpu_loads = malloc(4 * cpus_onln * sizeof(*cpu_loads));
    if (!cpu_loads)
        log_exit("malloc");
    cur_sysload->cpu_load_user = cpu_loads;
    cur_sysload->cpu_load_kernel = cpu_loads + cpus_onln;
    new_sysload->cpu_load_user = cpu_loads + 2 * cpus_onln;
    new_sysload->cpu_load_kernel = cpu_loads + 3 * cpus_onln;
    reset_sysload(cur_sysload);
    reset_sysload(new_sysload);
}

void swap_sysloads(void)
{
    struct loadgen_sysload *tmp = cur_sysload;
    cur_sysload = new_sysload;
    new_sysload = tmp;
}

void reset_sysload(struct loadgen_sysload *sysload)
{
    static struct cpu_load cpu_load = {-1, 0};
    int i;
    for (i = 0; i < cpus_onln; ++i) {
        sysload->cpu_load_user[i] = cpu_load;
        sysload->cpu_load_kernel[i] = cpu_load;
    }
    sysload->mem_pages_num = 0;
}

int loadgen_cpuload_empty(struct cpu_load *cpu_load)
{
    int i;
    for (i = 0; i < cpus_onln; ++i)
        if (cpu_load[i].cpu_num != -1)
            return 0;
    return 1;
}

int loadgen_sysload_empty(struct loadgen_sysload *sysload)
{
    if (sysload->mem_pages_num != 0)
        return 0;

    if (!loadgen_cpuload_empty(sysload->cpu_load_user) ||
        !loadgen_cpuload_empty(sysload->cpu_load_kernel))
        return 0;

    return 1;
}

void deallocate_memory(void)
{
    if (loadgen_mem == NULL || cur_sysload->mem_pages_num == 0)
        return;
    if (munmap(loadgen_mem, page_size * cur_sysload->mem_pages_num) < 0)
        log_exit("munmap");
    loadgen_mem = NULL;
}

void allocate_memory(void)
{
    if (loadgen_mem != NULL || cur_sysload->mem_pages_num == 0)
        return;
    loadgen_mem = mmap(0, page_size * cur_sysload->mem_pages_num,
                       PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
    if (loadgen_mem == MAP_FAILED)
        log_exit("mmap");
}

void get_block_devname(void)
{
    int root_major = -1;
    int root_minor = 0;
    struct stat statbuf = {0};
    FILE *diskfp;
    int major, minor;

    if (stat("/", &statbuf) < 0)
        log_exit("stat");
    root_major = major(statbuf.st_dev);

    diskfp = fopen("/proc/diskstats", "r");
    if (diskfp == NULL)
        log_exit("fopen");

    while (!feof(diskfp)) {
        fscanf(diskfp, "%d%d%s%*[^\n]", &major, &minor, devname + 5);
        if (major == root_major && minor == root_minor)
            return;
    }

    fprintf(stderr, "Couldn't resolve block device name.\nTerminating.\n");
    exit(EXIT_FAILURE);
}
