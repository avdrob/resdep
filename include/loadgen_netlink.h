#ifndef LOADGEN_NETLINK_H
#define LOADGEN_NETLINK_H

#include "loadgen_cpuload.h"

#define KMOD_NAME       "kloadgend"
#define NETLINK_LOADGEN  31

/* Struct for a packet to be sent via netlink socket. */
struct loadgen_packet_nl {
    enum nl_packet_type {
        NL_INIT,
        NL_CPU_LOAD,
        NL_RUN,
        NL_STOP
    } packet_type;
    struct cpu_load cpu_load;
};

extern void loadgen_netlink_init(void);
extern int loadgen_netlink_send(enum nl_packet_type, int cpu_num);

/* Structures for netlink socket we're using to communicate with kernel. */
extern int nl_sock_fd;
extern struct sockaddr_nl nl_src_addr, nl_dest_addr;
extern struct nlmsghdr *nlh, *nlh_ack;
extern struct loadgen_packet_nl *nl_packet;

#endif // LOADGEN_NETLINK_H
