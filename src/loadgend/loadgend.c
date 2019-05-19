#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <libgen.h>
#include <signal.h>
#include <pthread.h>

#include <sys/file.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <linux/netlink.h>

/* Used to separate declarations used solely by loadgend source itself. */
#define LOADGEND_SOURCE 1

#include "loadgen_conf.h"
#include "loadgen_log.h"
#include "loadgen_sysload.h"
#include "loadgen_unix.h"
#include "loadgen_netlink.h"
#include "loadgen_thread.h"

#define err_exit(msg)           \
        do {                    \
            perror(msg);        \
            exit(EXIT_FAILURE); \
        } while (0)

/* The name this program was invoked by. */
char *progname;

/* File descriptor to hold the file lock
 * (to ensure only one instance is running).
 */
static int lock_fd = -1;

/* No sense to run if there's no kernel support. */
static void check_kmod_is_loaded(void)
{
    FILE *modf = fopen("/proc/modules", "r");
    char modname[128];

    if (modf == NULL)
        err_exit("fopen");

    while (!feof(modf)) {
        fscanf(modf, "%s%*[^\n]", modname);
        if (strncmp(modname, KMOD_NAME, 128) == 0) {
            return;
        }
    }

    fprintf(stderr, "Module %s is not loaded.\nTerminating.\n", KMOD_NAME);
    exit(EXIT_FAILURE);
}

/* We use flock to ensure there's only one instance of the
 * daemon running at a time.
 */
static void check_no_instance_running(void)
{
    if ((lock_fd = open(LOADGEND_LOCK_NAME, O_WRONLY|O_CREAT, 0644)) < 0)
        err_exit("open");

    if (flock(lock_fd, LOCK_EX | LOCK_NB) < 0) {
        fprintf(stderr, "Another instance of %s is running.\nTerminating.\n",
                progname);
        exit(EXIT_FAILURE);
    }
}

static void loadgend_startup_checks(void)
{
    // check_kmod_is_loaded();
    check_no_instance_running();
}

/* Conforming mostly to APUE by Stevens, Rago. */
static void daemonize(void)
{
    pid_t pid;
    int fd, dirfd;
    DIR *dirp;
    struct dirent *dirent;
    char *endptr;

    umask(0033);

    /* Make sure we're not a process group leader and create new session. */
    if ((pid = fork()) < 0)
        err_exit("fork");
    else if (pid > 0)
        exit(EXIT_SUCCESS);
    setsid();

    /* Make sure we're not session leader. */
    if ((pid = fork()) < 0)
        err_exit("fork");
    else if (pid > 0)
        exit(EXIT_SUCCESS);

    if (chdir(LOADGEND_WORK_DIR) < 0)
        err_exit("chdir");

    /* Close all unneeded file descriptors. */
    if ((dirfd = open("/proc/self/fd", O_RDONLY|O_DIRECTORY)) < 0)
        err_exit("open");
    if ((dirp = fdopendir(dirfd)) == NULL)
        err_exit("opendir");
    while ((errno = 0, dirent = readdir(dirp)) != NULL) {
        if (dirent->d_type == DT_DIR)
            continue;

        if ((fd = (int) strtol(dirent->d_name, &endptr, 10)) == dirfd ||
             fd == lock_fd)
            continue;
        if (errno != 0 || *endptr != '\0')
            err_exit("strtol");

        close(fd);
    }
    if (errno != 0)
        err_exit("readdir");
    closedir(dirp);

    /* Open log. */
    if ((fd = open(LOADGEND_LOG_NAME, O_WRONLY|O_APPEND|O_CREAT, 0644)) < 0)
        exit(EXIT_FAILURE);
    if (dup(fd) < 0 || dup(fd) < 0)
        err_exit("dup");
    close(fd);

    /* Make sure streams are unbuffered. */
    if (setvbuf(stdout, NULL, _IONBF, 0) != 0 ||
        setvbuf(stderr, NULL, _IONBF, 0) != 0)
        log_err("setvbuf");
}

void loadgend_cleanup(void)
{
    if (is_running)
        free(is_running);
    if (loadgen_threads)
        free(loadgen_threads);
    if (nlh_ack)
        free(nlh_ack);
    if (nlh)
        free(nlh);
    if (lock_fd >= 0)
        close(lock_fd);
    if (un_conn_sock_fd >= 0)
        close(un_conn_sock_fd);
    if (nl_sock_fd >= 0)
        close(nl_sock_fd);
    if (cpu_loads)
        free(cpu_loads);
    deallocate_memory();
    pthread_attr_destroy(&pthread_attr);
    unlink(LOADGEND_SOCKET_NAME);
    unlink(LOADGEND_LOCK_NAME);
}

static void sig_handler(int signo)
{
    loadgend_cleanup();
    exit(EXIT_SUCCESS);
}

static void loadgend_init_resources(void)
{
    int i, ret;
    sigset_t sigset;
    struct sigaction sa;

    /* Block all possible signals except SIGTERM. */
    sigfillset(&sigset);
    sigdelset(&sigset, SIGTERM);
    if ((ret = pthread_sigmask(SIG_SETMASK, &sigset, NULL)) != 0) {
        errno = ret;
        log_exit("pthread_sigmask");
    }

    /* Handle SIGTERM: do some cleanup and die. */
    memset(&sa, 0, sizeof(struct sigaction));
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = sig_handler;
    if (sigaction(SIGTERM, &sa, NULL) < 0)
        log_exit("sigaction");

    /* Other threads use replacement for SIGTERM. */
    sa.sa_handler = thread_sigterm_handler;
    if (sigaction(LOADGEN_THREAD_TERM_SIGNAL, &sa, NULL) < 0)
        log_exit("sigaction");

    /* Init various modules. */
    loadgen_sysload_init();
    loadgen_unix_init();
    loadgen_netlink_init();
    loadgen_thread_init();
}

int main(int argc, char *argv[])
{
    progname = basename(argv[0]);
    loadgend_startup_checks();
    daemonize();
    loadgend_init_resources();
    loadgen_recv_loop();

    /* Unreachable. */
    loadgend_cleanup();
    return 0;
}
