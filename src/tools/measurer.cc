#include <iostream>
#include <fstream>
#include <vector>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <sched.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

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

using namespace std;

struct sys_load {
    float percent_cpu_user;
    float percent_cpu_kernel;
    float percent_mem;
    float percent_io;

    sys_load(vector<float> load)
        : percent_cpu_user(load[0]), percent_cpu_kernel(load[1]),
          percent_mem(load[2]), percent_io(load[3])
    {}
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

class measurer {
public:
    void measure()
    {
        current = 1 - current;
        finput.open(filename);
        internal_measure();
        finput.close();
    }

protected:
    const char *filename;
    ifstream finput;
    int current;

    measurer(const char *filename) : filename(filename), current(0) {}

    static unsigned long long diff(unsigned long long a,
                                   unsigned long long b)
    {
        return b > a ? b - a : 0;
    }

    static double share(unsigned long long val,
                        unsigned long long total)
    {
        return static_cast<double>(val) / static_cast<double>(total);
    }

private:
    virtual void internal_measure() = 0;
};

class time_measurer : public measurer {
public:
    time_measurer() : measurer("/proc/uptime"), uptime{0} {}

    unsigned long long get_interval() const
    {
        return diff(uptime[1 - current], uptime[current]);
    }

private:
    /* Uptime in jiffies. */
    unsigned long long uptime[2];

    virtual void internal_measure() override
    {
        unsigned long long secs, cents;

        finput >> secs;
        finput.get();
        finput >> cents;
        uptime[current] = secs * 100 + cents;
    }
};

class cpu_measurer : public measurer {
public:
    cpu_measurer() : measurer("/proc/stat"), cpustat{{0}, {0}} {}

    double get_user_percent() const
    {
        return 100 * share(diff(cpustat[1 - current].cpu_user,
                                cpustat[current].cpu_user),
                           diff(sum_cputime(cpustat[1 - current]),
                                sum_cputime(cpustat[current])));
    }

    double get_system_percent() const
    {
        return 100 * share(diff(cpustat[1 - current].cpu_sys +
                                cpustat[1 - current].cpu_softirq +
                                cpustat[1 - current].cpu_hardirq,
                                cpustat[current].cpu_sys +
                                cpustat[current].cpu_softirq +
                                cpustat[current].cpu_hardirq),
                           diff(sum_cputime(cpustat[1 - current]),
                                sum_cputime(cpustat[current])));
    }

private:
    struct cpu_stats {
        unsigned long long cpu_user;
        unsigned long long cpu_nice;
        unsigned long long cpu_sys;
        unsigned long long cpu_idle;
        unsigned long long cpu_iowait;
        unsigned long long cpu_steal;
        unsigned long long cpu_hardirq;
        unsigned long long cpu_softirq;
        unsigned long long cpu_guest;
        unsigned long long cpu_guest_nice;
    };
    cpu_stats cpustat[2];

    virtual void internal_measure() override
    {
        cpu_stats &stat = cpustat[current];

        finput.ignore(4, ' ');
        finput >> stat.cpu_user >> stat.cpu_nice >> stat.cpu_sys >>
                  stat.cpu_idle >> stat.cpu_iowait >> stat.cpu_hardirq >>
                  stat.cpu_softirq >> stat.cpu_steal >> stat.cpu_guest >>
                  stat.cpu_guest_nice;
    }

    unsigned long long sum_cputime(const cpu_stats &stat) const
    {
        return stat.cpu_user + stat.cpu_nice + stat.cpu_sys +
               stat.cpu_idle + stat.cpu_iowait + stat.cpu_steal +
               stat.cpu_hardirq + stat.cpu_softirq;
    }
};

class disk_measurer : public measurer {
public:
    disk_measurer() : measurer("/proc/diskstats"), diskstat{{0}, {0}}
    {
        struct stat statbuf = {0};
        stat("/", &statbuf);
        root_major = major(statbuf.st_dev);
    }

    double get_util_percent(unsigned long long itv) const
    {
        /* tot_ticks are in milliseconds, itv is in jiffies (0.01s). */
        return 10 * share(diff(diskstat[1 - current].rd_ticks,
                               diskstat[current].rd_ticks),
                          static_cast<double>(itv));
    }

private:
    struct io_stats {
        unsigned long long rd_ios;
        unsigned long long rd_merges;
        unsigned long long rd_sectors;
        unsigned long long rd_ticks;
        unsigned long long wr_ios;
        unsigned long long wr_merges;
        unsigned long long wr_sectors;
        unsigned long long wr_ticks;
        unsigned long long ios_pgr;
        unsigned long long tot_ticks;
        unsigned long long rq_ticks;
    };
    io_stats diskstat[2];

    int root_major;

    virtual void internal_measure() override
    {
        static unsigned int major, minor;
        static char devname[64];
        io_stats &stat = diskstat[current];

        while (!finput.eof()) {
            finput >> major >> minor;
            if (!(major == root_major && minor == 0)) {
                finput.ignore(256, '\n');
                continue;
            }
            finput >> devname >>
                      stat.rd_ios >> stat.rd_merges >> stat.rd_sectors >>
                      stat.rd_ticks >> stat.wr_ios >> stat.wr_merges >>
                      stat.wr_sectors >> stat.wr_ticks >> stat.ios_pgr >>
                      stat.tot_ticks >> stat.rq_ticks;
            return;
        }
    }
};

class memory_measurer : public measurer {
private:
    /* Note lexicographical order. */
    static constexpr const char *meminfo_fields[] = {"Buffers", "Cached",
                                                     "MemFree", "MemTotal",
                                                     "SReclaimable"};
    static constexpr size_t meminfo_fields_cnt = sizeof(meminfo_fields) /
                                                 sizeof(const char *);

    static int meminfo_bsearch(const char *field)
    {
        void *res = bsearch(static_cast<const void *>(&field),
                            static_cast<const void *>(meminfo_fields),
                            meminfo_fields_cnt,
                            sizeof(const char *),
                            [] (const void *lhs, const void *rhs) {
                                return strcmp(*((const char **) lhs),
                                              *((const char **) rhs));
                            });
        return res ? static_cast<const char **>(res) - meminfo_fields : -1;
    }

public:
    memory_measurer() : measurer("/proc/meminfo"), memstat{0} {}

    double get_mem_percent() const
    {
        unsigned long long mem_used = memstat.mem_total - memstat.mem_free -
                                      (memstat.buffers +
                                      (memstat.cached + memstat.slab_reclaim));
        return 100 * share(mem_used, memstat.mem_total);
    }

private:
    struct mem_stats {
        union {
            struct {
                /* Note lexicographical order. All values are in kB. */
                unsigned long long buffers;
                unsigned long long cached;
                unsigned long long mem_free;
                unsigned long long mem_total;
                unsigned long long slab_reclaim;
            };
            unsigned long long meminfo[meminfo_fields_cnt];
        };
    };
    mem_stats memstat;

    virtual void internal_measure() override
    {
        static char field[64];
        int cnt = 0;

        while (cnt < meminfo_fields_cnt && !finput.eof()) {
            int ind;
            finput.get(field, sizeof(field), ':');
            if ((ind = meminfo_bsearch(field))>= 0) {
                finput.get();
                finput >> memstat.meminfo[ind];
                cnt++;
            }
            finput.ignore(256, '\n');
        }
    }
};
constexpr const char *memory_measurer::meminfo_fields[];
constexpr size_t memory_measurer::meminfo_fields_cnt;

void __attribute__((constructor)) set_prio(void)
{
    struct sched_param param;

    param.sched_priority = 90;
    if (sched_setscheduler(0, SCHED_FIFO, &param) < 0) {
        perror("sched_setscheduler");
        exit(EXIT_FAILURE);
    }
}

void measure(vector<measurer *> meters)
{
    for (measurer *m : meters)
        m->measure();
}

int main(int argc, char *argv[])
{
    Loadgenctl loadgenctl;
    time_measurer tm;
    cpu_measurer cm;
    disk_measurer dm;
    memory_measurer mm;
    unsigned long long itv;

    vector<vector<float>> loads = {
                                    {28.5, 19.4, 13.0, 12.7},
                                    {13.8, 10.8, 15.4, 0.0},
                                    {11.4, 32.8, 14.9, 43.2},
                                    {27.3, 27.3, 18.5, 25.2},
                                    {25.2, 17.8, 20.0, 50.0}
                                  };

    cout.precision(2);
    cout << fixed;

    for (const auto load : loads) {
        loadgenctl.stop();
        loadgenctl.init();
        loadgenctl.send_load(sys_load(load));
        loadgenctl.run();

        sleep(50);
        measure({&tm, &cm, &dm, &mm});
        sleep(10);
        measure({&tm, &cm, &dm, &mm});
        itv = tm.get_interval();

        cout << load[0] << ' ' << load[1] << ' ' << load[2] << ' ' << load[3]
             << ": " << cm.get_user_percent() << ' ' << cm.get_system_percent()
             << ' ' << mm.get_mem_percent() << ' ' << dm.get_util_percent(itv)
             << endl;
    }

    // cout << "Uptime: " << itv << endl;
    // cout << "User%: " << cm.get_user_percent() << endl;
    // cout << "System%: " << cm.get_system_percent() << endl;
    // cout << "Disk%: " << dm.get_util_percent(itv) << endl;
    // cout << "Memory%: " << mm.get_mem_percent() << endl;

    return 0;
}
