#ifndef CPU_NL_H
#define CPU_NL_H

#define KMOD_NAME       "kloadgend"
#define NETLINK_LOADGEN  31

/* CPU load argument for both user processes & kernel threads */
struct cpu_load {
    unsigned int cpu_num;
    unsigned int load_msec;
};

/* Struct for a packet to be sent via netlink socket. */
struct nl_packet {
    /* Helps kernel module to determine which action to perform. */
    enum {
        NL_INIT,
        NL_CPU_LOAD,
        NL_RUN_THREADS,
        NL_STOP_THREADS
    } packet_type;
    struct cpu_load cpu_load;
};

#endif	/* CPU_NL_H */
