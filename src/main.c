#include <string.h>

#include "objects.h"
#include <sys/shm.h>
#include <sys/msg.h>
#include <unistd.h>

#include "dict.h"
#include "tools.h"
#include "menu.h"

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

    Fase 1: Utente entra (Codice user.c)
        L'utente (processo figlio) calcola quale stazione vuole (es. FIRST_COURSE).
        Accede a ctx->stations[0].queue.
        Si accoda: Inserisce il proprio indice (es. client_id = 5) nell'array queue.client_ids.
        Si mette in pausa (blocca su un semaforo privato o aspetta un flag nella SHM).
    Fase 2: Operatore Lavora (Codice worker.c)
        Il worker controlla ctx->stations[0].queue.
        Vede che c'è gente (count > 0).
        Estrae il primo ID dalla coda (es. 5).
        Legge i dati del cliente dall'array globale: client_t *c = &ctx->clients[5];
        Simula il servizio (nanosleep).
        Aggiorna i dati del cliente (c->taken_plates[...] = piatto_scelto).
        Sblocca il cliente (segnala il semaforo o setta un flag).
    Fase 3: Utente si sposta
        L'utente si sveglia.
        Controlla di essere stato servito.
        Aggiorna la sua current_location alla prossima tappa.
        Ripete il ciclo per la prossima stazione.
    Riassunto per voi
        Il codice che hai postato va bene ma togli dish_t menu[] (metti dimensione fissa) e togli gli include a queue.h e dict.h dentro objects.h (non puoi usare puntatori in SHM).
        Usate gli indici (int client_id) invece dei puntatori (client*) per mettervi in coda.
        Dividetevi così:
            Tu: Gestisci il movimento del client (decidere dove andare, mettersi in coda queue_push, aspettare).
            Bro: Gestisce il worker (prelevare dalla coda queue_pop, servire, aggiornare statistiche).
    */

    const int shm_id = shmget(IPC_PRIVATE, sizeof(SharedData), IPC_CREAT | 0666);
    if (shm_id < 0)
        panic("ERROR: Shared memory allocation is failed");

    SharedData* data = shmat(shm_id, NULL, 0);
    if (data == (void*)-1)
        panic("ERROR: Shared memory `at` is failed");

    // ----------------- STATIONS -----------------

    const int q_main_courses = msgget(IPC_PRIVATE, IPC_CREAT | 0666);
    if (q_main_courses < 0)
        panic("ERROR: Creation message queue `q_main_courses` is failed");

    const int q_first_courses = msgget(IPC_PRIVATE, IPC_CREAT | 0666);
    if (q_first_courses < 0)
        panic("ERROR: Creation message queue `q_first_courses` is failed");

    const int q_side_dishes = msgget(IPC_PRIVATE, IPC_CREAT | 0666);
    if (q_side_dishes < 0)
        panic("ERROR: Creation message queue `q_side_dishes` is failed");

    const int q_coffee_dishes = msgget(IPC_PRIVATE, IPC_CREAT | 0666);
    if (q_coffee_dishes < 0)
        panic("ERROR: Creation message queue `q_coffee_dishes` is failed");

    // ----------------- CHECKOUT -----------------

    const int q_checkout = msgget(IPC_PRIVATE, IPC_CREAT | 0666);
    if (q_checkout < 0)
        panic("ERROR: Creation message queue `q_checkout` is failed");


    // ----------------- CLEAN SHM -----------------
    memset(data, 0, sizeof(SharedData));
    data->is_sim_running = true;


    // ----------------- GET MENU -----------------
    load_menu("menu.json", data);

    for (size_t i = 0; i < data->main_menu_size; i++) {
        data->main_courses[i].id = data->main_courses_menu[i].id;
        data->main_courses[i].quantity = 100;
    }

    for (size_t i = 0; i < data->first_menu_size; i++) {
        data->first_courses[i].id = data->first_courses_menu[i].id;
        data->first_courses[i].quantity = 100;
    }

    for (size_t i = 0; i < data->side_menu_size; i++) {
        data->side_dishes[i].id = data->side_dish_menu[i].id;
        data->side_dishes[i].quantity = 100;
    }

    for (size_t i = 0; i < data->coffee_menu_size; i++) {
        data->coffee_dishes[i].id = data->coffee_menu[i].id;
        data->coffee_dishes[i].quantity = 100;
    }

    // CREATE WORKERS
    char str_shm_id[16];
    sprintf(str_shm_id, "%d", shm_id);

    char str_q_first[16];
    sprintf(str_q_first, "%d", q_first_courses);

    for (int i = 0; i < 2; i++) {
        const pid_t pid = fork();
        if (pid < 0) panic("ERROR: Fork worker failed");

        if (pid == 0) {
            char *args[] = { "./worker", str_shm_id, str_q_first, "PRIMI", NULL };

            execve("./worker", args, NULL);

            panic("ERROR: Execve failed for worker");
        }
    }
}
