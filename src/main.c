#include <stdio.h>

int main(void) {
    /*
    init:
        stations[]
        users_queue
        sim_ctx

    services_init(stations, users_queue)
        // primi, secondi, cassa, tavoli, dolci
        stations_init(stations)
        worker_task_init(stations) * NOF_WORKERS
        user_task_init(user_queue) * NOF_USERS

    wait_init_end()

    for i in 1..SIM_DURATION
        sim_day(stats);

    services_release()

    */
}
