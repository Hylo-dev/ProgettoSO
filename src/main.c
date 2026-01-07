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
}
