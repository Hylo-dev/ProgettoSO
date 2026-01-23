#ifndef _MSG_H
#define _MSG_H

#include "objects.h"
#include "tools.h"
#include <stddef.h>
#include <sys/msg.h>
#include <unistd.h>
#include <errno.h>

typedef enum {
    HIGH    = 1,
    TICKET  = 2,
    DEFAULT = 3
} priority_t;

typedef enum {
    ERROR                      = -1,
    RESPONSE_OK                =  0,
    REQUEST_OK                 =  1,
    RESPONSE_DISH_FINISHED     = -2,
    RESPONSE_CATEGORY_FINISHED = -3
} state_t;

typedef struct {
    long   mtype;  
    pid_t  client;
    int    status; 
    dish_t dish;
    size_t price;
    bool   ticket;
} msg_t;

static int
send_msg(
    const size_t     qid,
    const msg_t      msg,
    const size_t     msg_size
) {
    if (msgsnd((int)qid, &msg, msg_size, 0) == -1) {
        if (errno == EINTR || errno == EIDRM || errno == EINVAL) {
            return -1;
        }
        panic("ERROR: Message failed to send - queueid: %d, pid: %d, errno: %d", qid, getpid(), errno);
    }
    return 0;
}   

static int
recive_msg(
    const size_t qid,
    const long   mtype,
    msg_t *out
) {
    const size_t msg_size = sizeof(msg_t) - sizeof(long);

    const ssize_t read = msgrcv((int)qid, out, msg_size, mtype, 0);

    if (read < 0) {
        if (errno == EINTR || errno == EIDRM || errno == EINVAL) {
            return -1;
        }
        panic("ERROR: failed receiving a message - queue_id: %zu, pid: %d, mtype: %ld, errno: %d", 
               qid, getpid(), mtype, errno);
    }

    return 0;
}

static ssize_t
recv_msg_np(
    const size_t qid,
    const long   mtype,
    msg_t *out
) {
    const ssize_t m_size = sizeof(msg_t) - sizeof(long);
    ssize_t res = msgrcv((int)qid, out, m_size, mtype, 0);
    
    if (res < 0 && (errno == EIDRM || errno == EINVAL)) {
        return -1;
    }
    return res;
}

#endif // _MSG_H
