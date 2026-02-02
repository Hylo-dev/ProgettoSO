# Relazione: Simulazione Mensa "Oasi del Golfo"

**Corso di Sistemi Operativi 2025/26**

**Componenti del Gruppo:**

- Eliomar Alejandro Rodriguez Ferrer, 1166132
- Lorenzo Cavallero, 1163124

------

## 1. Architettura del Sistema

Il progetto simula il funzionamento di una mensa attraverso un'architettura multi-processo modulare, dove ogni entità è un processo separato che interagisce tramite meccanismi IPC.

### 1.1 Processi Implementati

- **Responsabile Mensa (`main.c`)**: Gestisce l'inizializzazione delle risorse IPC, crea i processi figli e monitora la simulazione. Si occupa del rifornimento periodico delle pietanze nelle stazioni in base ai parametri `AVG_REFILL` e gestisce l'arrivo dinamico di nuovi utenti tramite il segnale `SIGUSR2` inviato dall'eseguibile esterno `add_clients`.

- **Operatore (`worker.c`)**: Gestisce l'erogazione di primi, secondi e caffè. Gestisce le code di richieste e le pause individuali

- **Operatore Cassa (`worker.c`)**: Specializzazione del processo operatore che gestisce i pagamenti, calcola gli sconti per i ticket e aggiorna i ricavi globali.

- **Utente (`client.c`)**: Simula il comportamento del cliente. Sceglie il menu, attende nelle code delle stazioni, paga e consuma il pasto ai tavoli, gestendo anche la sincronizzazione con il proprio gruppo.

- **Communication Disorder (`disorder.c`)**: Processo esterno che, attivando un flag in SHM e agendo su un semaforo dedicato, blocca il funzionamento della stazione cassa per un tempo `DISORDER_DURATION`.

- **Aggiunta Utenti (`add_clients.c`)**: Strumento esterno che notifica al `main` la necessità di generare `n` nuovi processi utente in runtime tramite segnale.

  

### 1.2 Meccanismi di Comunicazione (IPC)

Come richiesto dai requisiti, sono stati utilizzati:

- **Memoria Condivisa (SHM)**: Utilizzata per ospitare la struttura globale `simctx_t` e l'array delle `station`. Questo permette a tutti i processi di accedere in tempo reale alle statistiche, alla disponibilità dei piatti e allo stato del disordine.

- **Semafori**: Impiegati per la mutua esclusione nell'accesso alla SHM (`sem[shm]`), per la gestione dei posti fisici ai tavoli (`sem[tbl]`) e per sincronizzare l'inizio e la fine della giornata lavorativa tra tutti i processi (`sem[wall]`, `sem[wk_end]`, `sem[cl_end]`).

- **Code di Messaggi**: Utilizzate per la comunicazione diretta e asincrona tra `client` e `worker`. Ogni stazione possiede una coda dedicata dove i clienti inviano richieste di tipo `msg_t` e attendono risposte basate sul proprio PID.

  

------

## 2. Scelte Progettuali e Politiche di Gestione

### 2.1 Associazione Operatori e Servizi

All'inizio di ogni giornata, il Responsabile Mensa distribuisce i `NOF_WORKERS` tra le stazioni.

- **Politica scelta**: Viene assegnato obbligatoriamente un operatore per ogni stazione per garantire il servizio minimo. I lavoratori rimanenti vengono distribuiti proporzionalmente ai tempi medi di servizio (`AVG_SRVC`): le stazioni più lente ricevono più personale per minimizzare i tempi di attesa degli utenti.

  

### 2.2 Gestione delle Pause degli Operatori

Gli operatori possono sospendere il servizio per un massimo di `NOF_PAUSE` volte al giorno.

- **Criterio di attivazione**: Un operatore ha una probabilità del 15% di entrare in pausa dopo aver servito un cliente. La pausa è concessa solo se nella stazione è presente almeno un altro operatore attivo, garantendo che nessuna stazione rimanga mai sguarnita, come richiesto dalle specifiche.

- **Sincronizzazione**: L'accesso alle postazioni di lavoro è regolato dal semaforo `wk_data.sem`, il cui valore iniziale corrisponde al numero di posti disponibili nella stazione (`nof_wk_seats`).

  

### 2.3 Comportamento dell'Utente e Menu

- **Scelta del Menu**: All'avvio della giornata, l'utente genera casualmente la propria lista dei desideri (primo, secondo e/o caffè) basandosi sul menu caricato in SHM.

- **Gestione Esaurimento**: Se un piatto specifico è esaurito, l'utente tenta di richiederne un altro della stessa categoria. Se l'intera categoria (es. tutti i primi) è terminata, l'utente prosegue verso la stazione successiva o, se non ha ottenuto nulla, abbandona la mensa.

  

------

## 3. Configurazione e Formato Dati

### 3.1 File di Configurazione

Il sistema utilizza file JSON per permettere modifiche ai parametri senza ricompilazione.

- **Sintassi**: Viene utilizzata la libreria `cJSON` per il parsing. Il file `default_config.json` definisce i parametri di base (durata, utenti, soglie di overload).

- **Struttura**:

  ```json
  { 
    "SIM_DURATION": 2,
    "N_NANO_SECS": 1000000,
    "OVERLOAD_THRESHOLD": 150,
  
    "NOF_WORKERS": 30,
    "NOF_USERS": 100,
    "MAX_USERS_PER_GROUP": 4,
    "NOF_PAUSE": 3,
    "PAUSE_DURATION": 30,
  
    "AVG_SRVC_PRIMI": 10,
    "AVG_SRVC_MAIN_COURSE": 15,
    "AVG_SRVC_COFFEE": 5,
    "AVG_SRVC_CASSA": 8,
  
    "NOF_WK_SEATS_PRIMI": 3,
    "NOF_WK_SEATS_SECONDI": 3,
    "NOF_WK_SEATS_COFFEE": 2,
    "NOF_WK_SEATS_CASSA": 2,
    "NOF_TABLE_SEATS": 80,
  
    "AVG_REFILL_PRIMI": 20,
    "AVG_REFILL_SECONDI": 20,
    "MAX_PORZIONI_PRIMI": 50,
    "MAX_PORZIONI_SECONDI": 50,
    "AVG_REFILL_TIME": 60,
    "DISORDER_DURATION": 1000,
    "N_NEW_USERS": 20
  }
  
  ```

  

### 3.2 Gestione del Menu

Il file `menu.json` definisce l'offerta gastronomica giornaliera.

- **Struttura**: 

  ```json
  {
    "first": [
      { "id": 0, "name": "Pasta al Pesto", "price": 5, "time": 10 },
      { "id": 1, "name": "Carbonara"     , "price": 7, "time": 12 }
    ],
    
    "main": [
      { "id": 0, "name": "Pollo Arrosto", "price": 8 , "time": 20 },
      { "id": 1, "name": "Bistecca"     , "price": 10, "time": 25 }
    ],
    
    "coffee": [
      { "id": 0, "name": "Espresso"		  , "price": 1, "time": 2   },
      { "id": 1, "name": "Cappuccino"   , "price": 2, "time": 4   },
      { "id": 2, "name": "Ginseng"	    , "price": 2, "time": 3   },
      { "id": 3, "name": "Decaffeinato" , "price": 1, "time": 2   }
    ]
  }
  
  ```

  

------

## 4. Versione Completa (Funzionalità Avanzate)

### 4.1 Communication Disorder

L'eseguibile `disorder` simula un'interruzione dei pagamenti. Quando attivo, imposta `is_disorder_active = true` e occupa il semaforo `disorder`. Gli operatori alla cassa, prima di processare un pagamento, verificano lo stato di questo flag e attendono sul semaforo, bloccando di fatto la coda finché il processo `disorder` non rilascia la risorsa dopo il tempo stabilito.

### 4.2 Utenti con Ticket e Gruppi

- **Ticket**: Gli utenti sono suddivisi tra possessori di ticket (80%) e non (20%). I possessori di ticket ricevono uno sconto del 12% (macro `DISCOUNT_DISH`) sul totale del pasto e utilizzano un `mtype` prioritario (`TICKET`) per essere serviti con precedenza alla cassa.

- **Gruppi**: Gli utenti possono appartenere a gruppi (fino a `MAX_USERS_PER_GROUP`). Utilizzano la struttura `groups_t` e un semaforo privato per sincronizzarsi: dopo il pagamento, i membri attendono che tutti i componenti del gruppo siano pronti prima di occupare i posti al tavolo e iniziare a mangiare contemporaneamente.

  

  ### Nota Progettuale sulla Gestione delle Code in Cassa

  Sebbene la specifica intenda l'esistenza di due file fisiche distinte per utenti con e senza ticket , in fase di implementazione si è optato per una **coda logica unica con gestione delle priorità** tramite il parametro `mtype` delle code di messaggi System V.

  

  Questa scelta è motivata dai seguenti vantaggi tecnici:

  - **Ottimizzazione della Concorrenza**: Utilizzare un unico canale IPC (Message Queue) evita la frammentazione dei processi operatore. Invece di avere operatori bloccati su due code diverse, tutti gli operatori alla cassa attingono dalla stessa risorsa, massimizzando il throughput del sistema.
  - **Gestione Nativa della Priorità**: Grazie alla funzione `msgrcv()`, gli operatori possono richiedere messaggi con un `mtype` specifico. Impostando una priorità numerica inferiore per i ticket (es. `TICKET = 2` rispetto a `DEFAULT = 3`), il kernel Unix garantisce che un utente con ticket venga servito prima di uno senza, simulando perfettamente l'effetto di una "corsia preferenziale" senza la complessità di gestire due code fisiche separate.
  - **Prevenzione del Deadlock e Starvation**: Una singola coda garantisce che nessun operatore rimanga inattivo se ci sono clienti in attesa, indipendentemente dal loro tipo, pur rispettando rigorosamente l'ordine di precedenza per i possessori di ticket.
  - **Semplificazione del Codice**: Riduce la necessità di sincronizzazione complessa tra processi che dovrebbero altrimenti monitorare due diverse code di messaggi, rendendo il sistema più robusto e meno propenso a errori di race condition.

### 4.3 Statistiche CSV

Al termine di ogni giornata, il `main` aggrega i dati raccolti dalle stazioni e scrive una riga nel file `data/stats.csv`. Vengono tracciati i ricavi, i piatti serviti, gli utenti non serviti e le medie giornaliere/globali per facilitare analisi post-simulazione.



------

## 5. Interfaccia Utente (TUI)

Il progetto integra un'interfaccia testuale (TUI) avanzata per fornire una rappresentazione visiva e immediata dello stato della simulazione in tempo reale, facilitando il monitoraggio dei processi e delle risorse.

### 5.1 Inizializzazione e Gestione del Terminale

La gestione dello schermo avviene tramite la funzione `init_scr()`, che si occupa di inizializzare il buffer video e di impostare il terminale in **Raw Mode**. Questa modalità è fondamentale per permettere al programma di intercettare i comandi da tastiera (come la pressione del tasto 'q' per la terminazione anticipata) senza attendere l'invio del tasto Invio. Al termine del programma, la funzione `kill_scr()` garantisce il ripristino delle impostazioni originali del terminale.

### 5.2 Dashboard in Tempo Reale

La funzione `render_dashboard` costituisce il modulo principale di visualizzazione durante l'esecuzione. Viene invocata periodicamente (ogni `DASHBOARD_UPDATE_RATE` minuti simulati) o in seguito a eventi specifici come l'aggiunta di nuovi utenti. La dashboard è suddivisa in diverse aree informative:

- **Header Informativo**: Mostra il giorno corrente e una barra di avanzamento dinamica che indica il rapporto tra utenti all'interno della mensa e utenti totali previsti.
- **Statistiche di Flusso e Staff**: Fornisce dati aggregati su utenti serviti, utenti non serviti, occupazione dei tavoli e numero totale di pause effettuate dal personale.
- **Situazione Cucina**: Visualizza in tempo reale le scorte residue di ogni stazione di distribuzione (Primi e Secondi), segnalando visivamente quando le porzioni scarseggiano.
- **Efficienza Stazioni**: Mostra per ogni stazione il tempo medio di servizio, il numero di operatori attivi e una barra grafica che rappresenta lo stato di ogni singolo lavoratore (attivo o in pausa).
- **System Status**: Include un indicatore pulsante (battito) che cambia schema visivo in caso di attivazione del `disorder`, segnalando graficamente lo stato di allerta.



------

## 6. Note Implementative e Codice Esterno

- **Compilazione**: Il progetto viene compilato tramite `Makefile` con i flag di sicurezza `-Wvla -Wextra -Werror -D_GNU_SOURCE`.
- **Targets di Compilazione**:
  - `clean` pulisce build vecchie per una nuova compilazione pulita.
  - `run-main` esegue la simulazione usando il config passato come argomento, altrimenti quelli di default.
  - `run-disorder` esegue l'eseguibile generato dal file `disorder.c` per lanciare il disorder nella simulazione.
  - `run-add-clients` esegue l'eseguibile generato dal file `add_clients.c` per aggiungere clients alla simulazione.
- **Codice Esterno**: È stata integrata la libreria open-source **cJSON** per la gestione semplificata dei dati strutturati nei file di configurazione e menu.
- **Gestione Risorse**: Il sistema implementa una pulizia rigorosa. In caso di terminazione (normale, `SIGINT` o overload), il processo padre invia `SIGUSR1` ai figli e dealloca semafori, code di messaggi e segmenti di memoria condivisa prima di uscire.
- **Modularità della TUI**: L'interfaccia è stata progettata in modo astratto tramite un oggetto `screen`. Questo permette di disegnare forme primitive (box, linee, barre di progresso) e testo posizionato in modo assoluto, rendendo il codice del `main.c` pulito e focalizzato sulla logica di aggiornamento dei dati piuttosto che sulla manipolazione diretta delle sequenze di escape del terminale.