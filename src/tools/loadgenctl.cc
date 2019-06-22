#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "loadgen_conf.h"
#include "loadgen_unix.h"

#define err_exit(msg)                                \
        do {                                         \
            perror(msg);                             \
            exit(EXIT_FAILURE);                      \
        } while (0)

#define log_format(format, ...)                      \
        do {                                         \
            fprintf(stderr, format, __VA_ARGS__);    \
            fprintf(stderr, ": ");                   \
            perror("");                              \
        } while (0)

#define err_format_exit(format, ...)                 \
        do {                                         \
            log_format(format, __VA_ARGS__);         \
            exit(EXIT_FAILURE);                      \
        } while (0)

struct sys_load {
    float percent_cpu_user;
    float percent_cpu_kernel;
    float percent_mem;
    float percent_io;
};

class Loadgenctl {
public:
    static constexpr struct sockaddr_un dest_addr = {
                                            AF_UNIX,
                                            LOADGEND_SOCKET_NAME
                                        };

    Loadgenctl()
    {
        if ((sockfd = socket(AF_UNIX, SOCK_SEQPACKET, 0)) < 0)
            err_format_exit("socket(%s)", LOADGEND_SOCKET_NAME);
        if (connect(sockfd, (struct sockaddr *) &dest_addr,
                    sizeof(dest_addr)) < 0)
            err_format_exit("connect(%s)", LOADGEND_SOCKET_NAME);
    }

    ~Loadgenctl() { close(sockfd); }

    void init() const
    {
        static struct loadgen_packet_un un_packet = {{0}, UN_INIT};
        internal_send(un_packet);
    }

    void send_load(const struct sys_load &sys_load) const
    {
        static struct loadgen_packet_un un_packet = {0};

        if (sys_load.percent_cpu_user > 0) {
            un_packet.percent = sys_load.percent_cpu_user;
            un_packet.packet_type = UN_CPU_USER;
            for (int i = 0; i < 4; ++i) {
                un_packet.cpu_num = i;
                internal_send(un_packet);
            }
        }

        if (sys_load.percent_cpu_kernel > 0) {
            un_packet.percent = sys_load.percent_cpu_kernel;
            un_packet.packet_type = UN_CPU_KERNEL;
            for (int i = 0; i < 4; ++i) {
                un_packet.cpu_num = i;
                internal_send(un_packet);
            }
        }

        un_packet.packet_type = UN_MEM;
        un_packet.percent = sys_load.percent_mem;
        internal_send(un_packet);

        un_packet.packet_type = UN_IO;
        un_packet.percent = sys_load.percent_io;
        internal_send(un_packet);
    }

    void run() const
    {
        static struct loadgen_packet_un un_packet = {{0}, UN_RUN};
        internal_send(un_packet);
    }

    void stop() const
    {
        static struct loadgen_packet_un un_packet = {{0}, UN_STOP};
        internal_send(un_packet);
    }

private:
    int sockfd;

    void internal_send(const loadgen_packet_un &un_packet) const
    {
        static loadgen_packet_un un_response = {0};

        if (send(sockfd, (const void *) &un_packet, sizeof(un_packet), 0) < 0)
            err_format_exit("send(%d)", un_packet.packet_type);
        if (recv(sockfd, (void *) &un_response,
                 sizeof(un_response), 0) < 0)
            log_format("recv(%d)", un_packet.packet_type);
        if (un_response.packet_type != UN_OK)
            log_format("loadgend: %s", un_response.errmsg);
    }
};
constexpr struct sockaddr_un Loadgenctl::dest_addr;

int main(int argc, char *argv[])
{
    Loadgenctl loadgenctl;

    loadgenctl.stop();
    loadgenctl.init();
    loadgenctl.send_load({13.8, 10.8, 15.4, 0.0});
    loadgenctl.run();

    // sleep(15);

    // loadgenctl.stop();
    // loadgenctl.init();
    // loadgenctl.send_load({23.2, 18.4, 19.0, 30.0});
    // loadgenctl.run();

    return 0;
}
