#ifndef _CONST_H
#define _CONST_H

/* =================== CONST =================== */
#define NOF_STATIONS 4
#define MAX_DISHES 3
#define DISCOUNT_DISH 12

/* =================== DISH MAX =================== */
#define MAX_MAIN 10
#define MAX_FIRST 10
#define MAX_COFFEE 10

#define MAX_ELEMENTS 10

/* =================== SIM. DURATION =================== */
#define WORK_DAY_MINUTES 480 /* 8h */
#define N_NANO_SECS 10000000 /* old: 5, number of real ns for a minute in the sim */
#define TO_NANOSEC 1000000000L

/* =================== AVERAGE TIMES =================== */
static unsigned long var_srvc[] = {
    50, // VAR_SRVC_FIRST_COURSE 
    50, // VAR_SRVC_MAIN_COURSE 
    80, // VAR_SRVC_COFFEE 
    20, // VAR_SRVC_CHECKOUT 
    20, // VAR_SRVC_REFILL 
};

/* ====================== SIM DATA ===================== */
#define DISHES_COUNT 10
#define REFILL_INTERVAL 10

#define DASHBOARD_UPDATE_RATE 30
#define TUI_NOTIFICATIONS_LEN 10

#endif
