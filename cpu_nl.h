#ifndef CPU_NL_H
#define CPU_NL_H

#define KMOD_NAME       "kcpuhog"
#define NETLINK_CPUHOG  31

/* Helps kernel module to determine which action to perform. */
enum nl_packet_type {
    NL_THREADS_NUM,
    NL_CPU_LOAD,
    NL_STOP_THREADS
};

/* CPU load argument for both user processes & kernel threads */
struct cpu_load {
    unsigned int cpu_num;
    unsigned int load_msec;
};

/* Struct for a packet to be sent via netlink socket. */
struct nl_packet {
    enum nl_packet_type packet_type;
    union {
        int threads_num;
        struct cpu_load cpu_load;
    };
};

#endif	/* CPU_NL_H */
