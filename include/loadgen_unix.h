#ifndef LOADGEN_UNIX_H
#define LOADGEN_UNIX_H

enum un_packet_type {
    UN_INIT,
    UN_CPU_USER,
    UN_CPU_KERNEL,
    UN_MEM,
    UN_RUN,
    UN_STOP,
    UN_ERR,
    UN_OK
};

struct loadgen_packet_un {
    union {
        struct {
            float percent;
            int cpu_num;
        };
        char errmsg[64];
    };
    enum un_packet_type packet_type;
};

#ifdef LOADGEND_SOURCE
/* Structures for UNIX socket we're listening on.*/
extern int un_conn_sock_fd;
extern struct sockaddr_un un_addr;

extern void loadgen_unix_init(void);
extern void loadgen_recv_loop(void);
#endif // LOADGEND_SOURCE

#endif // LOADGEN_UNIX_H
