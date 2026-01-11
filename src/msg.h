#ifndef _MSG_H
#define _MSG_H

#include "objects.h"
#include "tools.h"
#include <stddef.h>
#include <stdint.h>
#include <sys/msg.h>
#include <unistd.h>


typedef struct {
    long   mtype;  // 1: HIGH PRIORITY, 2: TICKET, 3: NORMAL USER
    pid_t  client;
    int    status; // -1: error, 0: OK (response), 1: ok (request)
    dish_t dish;
} msg_dish_t;


static int
send_msg(
    const size_t     qid,
    const msg_dish_t msg,
    const size_t     msg_size
) {

    if (msgsnd((int)qid, &msg, msg_size, 0) == -1)
        panic("ERROR: Message failed to send - queueid: %d, pid: %d", qid, getpid());
    return 0;
}   

static int
recive_msg(
    const size_t qid,
    const long   mtype,
    msg_dish_t *out
) {
    const size_t msg_size = sizeof(msg_dish_t)-sizeof(long);

    const ssize_t read = msgrcv((int)qid, out, msg_size, mtype, 0);

    if (read < 0)
        panic("ERROR: failed sending a message - queue_id: %zu, pid: %d, mtype: %d, dishid: %d", qid, getpid(), mtype, out->dish.id);

    return 0;
}


#endif // _MSG_H
