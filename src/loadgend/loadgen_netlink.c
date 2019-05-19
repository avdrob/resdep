#include <asm/types.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <string.h>

#include "loadgen_log.h"
#include "loadgen_sysload.h"
#include "loadgen_netlink.h"

int nl_sock_fd = -1;
struct sockaddr_nl nl_src_addr, nl_dest_addr;
struct nlmsghdr *nlh = NULL, *nlh_ack = NULL;
struct loadgen_packet_nl *nl_packet = NULL;

void loadgen_netlink_init(void)
{
    /* Setup netlink socket. */
    if ((nl_sock_fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_LOADGEN)) < 0)
        log_exit("socket");
    nl_src_addr = (struct sockaddr_nl) { AF_NETLINK, 0, getpid(), 0 };
    nl_dest_addr = (struct sockaddr_nl) { AF_NETLINK, 0, 0, 0 };
    if (bind(nl_sock_fd, (struct sockaddr *) &nl_src_addr,
             sizeof(struct sockaddr_nl)) < 0)
        log_exit("bind");

    /* Init netlink message headers. */
    nlh_ack = (struct nlmsghdr *) malloc(NLMSG_SPACE(sizeof(struct nlmsgerr)));
    nlh = (struct nlmsghdr *) malloc(NLMSG_SPACE(sizeof(*nl_packet)));
    if (!nlh || !nlh_ack)
        log_exit("malloc");
    nlh->nlmsg_len   = NLMSG_LENGTH(sizeof(*nl_packet));
    nlh->nlmsg_type  = NLMSG_NOOP;
    nlh->nlmsg_flags = NLM_F_ACK;
    nlh->nlmsg_pid   = getpid();

    /* We'll put data at the same address every time. */
    nl_packet = (struct loadgen_packet_nl *) NLMSG_DATA(nlh);
}

int loadgen_netlink_send(enum nl_packet_type packet_type, int cpu_num)
{
    struct nlmsgerr *err;

    switch (packet_type) {
    case NL_INIT:
        nlh->nlmsg_seq = 0;
        break;
    case NL_CPU_LOAD:
        if (cpu_num < 0 || cpus_onln <= cpu_num) {
            log_format("invalid CPU num: %d", cpu_num);
            return -1;
        }
        nlh->nlmsg_seq++;
        memcpy(&(nl_packet->cpu_load), &(new_sysload->cpu_load_kernel[cpu_num]),
               sizeof(nl_packet->cpu_load));
        break;
    case NL_RUN:
    case NL_STOP:
        nlh->nlmsg_seq++;
        break;
    default:
        log_format("unknown netlink packet type: %d", packet_type);
        return -1;
        break;
    }

    nl_packet->packet_type = packet_type;
    if (sendto(nl_sock_fd, (void *) nlh, nlh->nlmsg_len, 0, (struct sockaddr *)
               &nl_dest_addr, sizeof(nl_dest_addr)) < 0) {
        log_err("sendto");
        return -1;
    }

    /* Now receive an acknowledgement from kernel. */
    if (recv(nl_sock_fd, (void *) nlh_ack,
             NLMSG_LENGTH(sizeof(struct nlmsgerr)), 0) < 0) {
        log_err("recv");
        return -1;
    }

    if (nlh->nlmsg_seq != nlh_ack->nlmsg_seq) {
        log_format("nlmsg sequence number mismatch: %d instead of %d",
                   nlh_ack->nlmsg_seq, nlh->nlmsg_seq);
        return -1;
    }
    if (nlh_ack->nlmsg_type != NLMSG_ERROR) {
        log("nlmsg type mismatch: should be NLMSG_ERROR");
        return -1;
    }
    else {
        err = (struct nlmsgerr *) NLMSG_DATA(nlh_ack);
        if (err->error != 0) {
            log_format("nlmsg error == %d instead of %d\n", err->error, 0);
            return -1;
        }
    }

    return 0;
}
