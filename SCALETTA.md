# toimplement

``` C
struct queue;
struct stazione_primi {
    size_t tot_time;
    
};
struct stazione_secondi;
struct stazione_dolci;
struct stazione_cassa;

strcut {
  size_t primi, secondi-contorni, dolce-caffe;
} piatti_cnt;

strcut stats {
    size_t unserved;
    size_t serverd;
    piatti_cnt serviti;
    piatti_cnt avanzati;
    size_t operatori_attivi;
    size_t pause_cnt;
    size_t *guadagni;
};

struct cliente {
    int id;
    location l;
    bool ticket;
    int  wait_time;
}
enum piatti {
    primi, secondi, dolci
}
enum location {
    primi, secondi, dolci, cassa, tavolo
}

```

include/
  ...
src/
  main.c
  queue.c
  array.c
  structs.c
  file_parser.c
  
Makefile

