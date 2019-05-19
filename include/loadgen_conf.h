#ifndef LOADGEN_CONF_H
#define LOADGEN_CONF_H

#define LOADGEND_WORK_DIR       "/tmp"
#define LOADGEND_SOCKET_NAME    LOADGEND_WORK_DIR "/loadgend.socket"

#ifdef LOADGEND_SOURCE
#define LOADGEND_LOG_NAME       LOADGEND_WORK_DIR "/loadgend.log"
#define LOADGEND_LOCK_NAME      LOADGEND_WORK_DIR "/loadgend.lock"
#endif // LOADGEND_SOURCE

#endif // LOADGEN_CONF_H
