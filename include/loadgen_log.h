#ifndef LOADGEN_LOG_H
#define LOADGEN_LOG_H

#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <sys/syscall.h>

extern void loadgend_cleanup(void);
extern char *progname;

/* Buffer to hold current timestamp.
 * Unique for each thread.
 */
static __thread char timestamp[64];

static inline void update_timestamp(void)
{
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    strftime(timestamp, sizeof(timestamp), "%b %d %H:%M:%S", tm);
}

/* Buffer to hold error message.
 * Unique for each thread.
 */
static __thread char err_buf[256];

/* Glibc perror isn't thread-safe.
 * Thus we combine strerror_r and fprintf, which are.
 */
static inline void perror_r(const char *msg)
{
    size_t len;
    int errnum = errno;

    if (strerror_r(errnum, err_buf, sizeof(err_buf)) != 0)
        return;

    len = strnlen(err_buf, sizeof(err_buf) - 1);
    if (0 < len) {
        if (err_buf[len - 1] == '\n')
            err_buf[len - 1] = '\0';
        else
            err_buf[len] = '\0';
    }
    err_buf[sizeof(err_buf) - 1] = '\0';
    fprintf(stderr, "%s: %s\n", msg, err_buf);
}

#define log(msg)                                            \
        do {                                                \
            update_timestamp();                             \
            fprintf(stderr, "%s %s[%d]: %s\n",              \
                    timestamp, progname, getpid(), msg);    \
        } while(0)

#define log_format(format, ...)                             \
        do {                                                \
            update_timestamp();                             \
            fprintf(stderr, "%s %s[%d]: ",                  \
                    timestamp, progname, getpid());         \
            fprintf(stderr, format, __VA_ARGS__);           \
            fprintf(stderr,  "\n");                         \
        } while (0)

#define log_err(msg)                                        \
        do {                                                \
            update_timestamp();                             \
            fprintf(stderr, "%s %s[%d]: ",                  \
                    timestamp, progname, getpid());         \
            perror_r(msg);                                  \
        } while (0)

#define log_exit(msg)                                       \
        do {                                                \
            log_err(msg);                                   \
            loadgend_cleanup();                             \
            exit(EXIT_FAILURE);                             \
        } while(0)

#define gettid()    syscall(__NR_gettid)

#define thread_log(msg)                                     \
        do {                                                \
            update_timestamp();                             \
            fprintf(stderr, "%s %s[%d:%ld]: %s\n",          \
                    timestamp, progname, getpid(),          \
                    gettid(), msg);                         \
        } while(0)

#define thread_log_err(msg)                                 \
        do {                                                \
            update_timestamp();                             \
            fprintf(stderr, "%s %s[%d:%ld]: ", timestamp,   \
                    progname, getpid(), gettid());          \
            perror_r(msg);                                  \
        } while(0)

#define thread_log_exit(msg)                                \
        do {                                                \
            thread_log_err(msg);                            \
            pthread_exit(NULL);                             \
        } while(0)

#endif // LOADGEN_LOG_H
