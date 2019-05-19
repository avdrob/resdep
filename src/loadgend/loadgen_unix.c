#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <string.h>

#include "loadgen_conf.h"
#include "loadgen_log.h"
#include "loadgen_sysload.h"
#include "loadgen_unix.h"
#include "loadgen_netlink.h"
#include "loadgen_thread.h"

int un_conn_sock_fd = -1;
struct sockaddr_un un_addr = {0};

#define MSEC_PER_SEC    1000

void loadgen_unix_init(void)
{
    /* Setup UNIX socket. */
    unlink(LOADGEND_SOCKET_NAME);
    if ((un_conn_sock_fd = socket(AF_UNIX, SOCK_SEQPACKET, 0)) < 0)
        log_exit("socket");
    memset(&un_addr, 0, sizeof(struct sockaddr_un));
    un_addr.sun_family = AF_UNIX;
    strncpy(un_addr.sun_path, LOADGEND_SOCKET_NAME,
            sizeof(un_addr.sun_path) - 1);
    if (bind(un_conn_sock_fd, (const struct sockaddr *) &un_addr,
             sizeof(struct sockaddr_un)) < 0)
        log_exit("bind");
}

static void process_packet(int un_data_sock_fd,
                           struct loadgen_packet_un *un_packet)
{
    enum { STATUS_OK, STATUS_ERR } status = STATUS_OK;
    int cpu_num;
    int load_msec_user, load_msec_kernel;
    int ret;
    float percent;

#define PROCESS_PACKET_ERR(format, ...)                             \
        do {                                                        \
            log_format(format, __VA_ARGS__);                        \
            snprintf(un_packet->errmsg,                             \
                     sizeof(un_packet->errmsg),                     \
                     format, __VA_ARGS__);                          \
            status = STATUS_ERR;                                    \
            goto send_response;                                     \
        } while (0)

#define CHECK_PERCENT_VALUE                                         \
        do {                                                        \
            percent = un_packet->percent;                           \
            if (percent < 0.0 || percent >= 100.0)                  \
                PROCESS_PACKET_ERR("invalid percent value: %f",     \
                                   percent);                        \
        } while(0)

#define CHECK_CPU_NUM                                               \
        do {                                                        \
            cpu_num = un_packet->cpu_num;                           \
            if (cpu_num < 0 || cpus_onln <= cpu_num)                \
                PROCESS_PACKET_ERR("invalid CPU num: %d", cpu_num); \
        } while(0)

#define SEND_TO_KERNEL(type, cpu)                                   \
        do {                                                        \
            if (loadgen_netlink_send(type, cpu) < 0)                \
                PROCESS_PACKET_ERR("error while sending message "   \
                                   "to %s. Check logs for further " \
                                   "details", KMOD_NAME);           \
        } while(0)

    switch(un_packet->packet_type) {
    case UN_INIT:
        SEND_TO_KERNEL(NL_INIT, -1);
        reset_sysload(new_sysload);
        break;
    case UN_CPU_USER:
        CHECK_PERCENT_VALUE;
        CHECK_CPU_NUM;

        load_msec_user = percent_to_msec(percent);
        load_msec_kernel = new_sysload->cpu_load_kernel[cpu_num].load_msec;
        if (load_msec_user + load_msec_kernel >= MSEC_PER_SEC)
            PROCESS_PACKET_ERR("user(%d) + kernel(%d) load exceeds 100%%",
                               load_msec_user, load_msec_kernel);

        new_sysload->cpu_load_user[cpu_num].cpu_num = cpu_num;
        new_sysload->cpu_load_user[cpu_num].load_msec = load_msec_user;
        break;
    case UN_CPU_KERNEL:
        CHECK_PERCENT_VALUE;
        CHECK_CPU_NUM;

        load_msec_kernel = percent_to_msec(percent);
        load_msec_user = new_sysload->cpu_load_user[cpu_num].load_msec;
        if (load_msec_user + load_msec_kernel >= MSEC_PER_SEC)
            PROCESS_PACKET_ERR("user(%d) + kernel(%d) load exceeds 100%%",
                               load_msec_user, load_msec_kernel);

        SEND_TO_KERNEL(NL_CPU_LOAD, cpu_num);
        new_sysload->cpu_load_kernel[cpu_num].cpu_num = cpu_num;
        new_sysload->cpu_load_kernel[cpu_num].load_msec = load_msec_kernel;
        break;
    case UN_MEM:
        CHECK_PERCENT_VALUE;
        new_sysload->mem_pages_num = percent_to_pages_num(percent);
        break;
    case UN_RUN:
        if (loadgen_sysload_empty(new_sysload))
            PROCESS_PACKET_ERR("%s", "no system load specified");
        if (!loadgen_sysload_empty(cur_sysload))
            PROCESS_PACKET_ERR("system is already loaded; you need to send "
                               "%s first", "UN_STOP");
        swap_sysloads();
        reset_sysload(new_sysload);
        if (!loadgen_cpuload_empty(cur_sysload->cpu_load_kernel))
            SEND_TO_KERNEL(NL_RUN, -1);
        allocate_memory();
        loadgen_run_threads();
        break;
    case UN_STOP:
        SEND_TO_KERNEL(NL_STOP, -1);
        loadgen_kill_threads();
        deallocate_memory();
        reset_sysload(cur_sysload);
        break;
    case UN_ERR:
        PROCESS_PACKET_ERR("invalid packet type: %s", "UN_ERR");
        break;
    case UN_OK:
        log("got packet of type UN_OK");
        break;
    default:
        PROCESS_PACKET_ERR("unknown unix packet type: %d",
                           un_packet->packet_type);
        break;
    }

#undef SEND_TO_KERNEL
#undef CHECK_CPU_NUM
#undef CHECK_PERCENT_VALUE
#undef PROCESS_PACKET_ERR

send_response:
    switch (status) {
    case STATUS_OK:
        un_packet->packet_type = UN_OK;
        un_packet->errmsg[0] = '\0';
        break;
    case STATUS_ERR:
        un_packet->packet_type = UN_ERR;
        un_packet->errmsg[sizeof(un_packet->errmsg) - 1] = '\0';
        break;
    default:
        break;
    }

    ret = send(un_data_sock_fd, (void *) un_packet,
               sizeof(*un_packet), MSG_NOSIGNAL);
    if (ret < 0)
        log_err("send");
    else if (ret < sizeof(*un_packet))
        log_format("invalid sent packet size: %db/%ldb",
                   ret, sizeof(*un_packet));
}

void __attribute__((noreturn)) loadgen_recv_loop(void)
{
    ssize_t ret;
    struct loadgen_packet_un un_packet;

    if (listen(un_conn_sock_fd, 1) < 0)
        log_exit("listen");

    for (;;) {
        int un_data_sock_fd;

        if ((un_data_sock_fd = accept(un_conn_sock_fd, NULL, NULL)) < 0) {
            log_err("accept");
            continue;
        }

        for (;;) {
            ret = recv(un_data_sock_fd, (void *) &un_packet,
                       sizeof(un_packet), MSG_WAITALL);

            if (ret < 0) {
                log_err("recv");
                continue;
            }
            else if (ret == 0) {
                /* Connection is closed. */
                close(un_data_sock_fd);
                break;
            }
            else if (0 < ret && ret < sizeof(un_packet)) {
                log_format("invalid received packet size: %lub/%ldb",
                           ret, sizeof(un_packet));
                continue;
            }

            process_packet(un_data_sock_fd, &un_packet);
        }
    }
}

