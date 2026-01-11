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
    size_t qid,
    long   mtype,
    pid_t  pid,
    size_t dish_id
) {
    msg_dish_t msg = {
        mtype,
        pid,
        0,
        (dish_t){ dish_id, "", 0, 0}
    };

    size_t msg_size = sizeof(msg_dish_t)-sizeof(long);

    if (msgsnd((int)qid, &msg, msg_size, 0) == -1)
        panic("ERROR: Message failed to send - queueid: %d, pid: %d", qid, getpid());
    return 0;
}   

static int
recive_msg(
    size_t qid,
    long   mtype,
    msg_dish_t *out
) {
    size_t msg_size = sizeof(msg_dish_t)-sizeof(long);

    ssize_t read = msgrcv((int)qid, out, msg_size, mtype, 0);

    if (read < 0)
        panic("ERROR: failed sending a message - queueid: %zu, pid: %d, mtype: %d, dishid: %d", qid, getpid(), mtype, out->dish.id);

    return 0;
}


#endif // _MSG_H
