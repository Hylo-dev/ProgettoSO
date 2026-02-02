# Mensa "Oasi del Golfo" - Simulator (2025/26)

Progetto per l'esame di **Sistemi Operativi** focalizzato sulla gestione della concorrenza, IPC (Inter-Process Communication) e sincronizzazione in ambiente Unix.



## Team

- **Eliomar Alejandro Rodriguez Ferrer**, 1166132
- **Lorenzo Cavallero**, 1163124



## Requisiti Implementativi

Il simulatore rispetta i seguenti requisiti tecnici:

- **Multi-processo**: Divisione in moduli tramite processi distinti lanciati con `execve`.

- **IPC**: Utilizzo di Memoria Condivisa, Semafori e Code di Messaggi.

- **No Attesa Attiva**: La sincronizzazione è gestita interamente tramite semafori e chiamate bloccanti.

- **Parallelismo**: Il progetto è testato per eseguire su macchine multi-processore.

  

## Compilazione

Per compilare l'intero progetto, è necessario utilizzare l'utility `make`. Verranno creati gli eseguibili necessari all'interno della cartella `bin/`.

```bash
# Per compilare il progetto pulendo compilazioni vecchie
make clean && make
```



## Modalità di Esecuzione

### 1. Avvio Simulazione Principale

Il processo `responsabile_mensa` (main) accetta opzionalmente un file di configurazione.

```bash
# Esecuzione con configurazione di default
make run

# Esecuzione con file specifico
make run ARGS=filepath.json
```



### 2. Strumenti Esterni (Versione Completa)

Mentre la simulazione è in esecuzione, è possibile invocare i seguenti strumenti da terminali separati:

- **Communication Disorder**: Blocca i pagamenti alla cassa per un tempo predefinito.

  ```bash
  make run-disorder
  ```

- **Aggiunta Utenti**: Invia un segnale `SIGUSR2` al main per generare istantaneamente nuovi processi utente.

  ```bash
  make run-add-client ARGS=number
  ```



## Struttura della Consegna

- `/data`: Contiene i file `.json` per la configurazione, il menu e i file persistenti come `stats.csv` e `simulation.log`.
- `/bin`: Destinazione degli eseguibili compilati.
- `/src`: Codice sorgente del progetto (`main.c`, `client.c`, `worker.c`, ecc.).
- `Relazione_Rodriguez_Cavallero.pdf`: Documentazione tecnica dettagliata.



## Terminazione e Statistiche

La simulazione termina automaticamente al raggiungimento della durata (`SIM_DURATION`) o per superamento della soglia di `OVERLOAD_THRESHOLD`. Al termine, verranno stampate a video le statistiche finali e i dati verranno salvati in formato CSV per analisi esterne.