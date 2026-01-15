#ifndef _CONST_H
#define _CONST_H

/* =================== CONST =================== */
#define NOF_STATIONS 4
#define MAX_DISHES 3

/* =================== DISH MAX =================== */
#define MAX_MAIN 10
#define MAX_FIRST 10
#define MAX_COFFEE 10

#define MAX_ELEMENTS 10

/* =================== SIM. DURATION =================== */
#define SIM_DURATION 5 /* days */
#define N_NANO_SECS 5 /* number of real ns for a minute in the sim */

/* =================== AVERAGE TIMES =================== */
static double var_srvc[] = {
    0.5, // VAR_SRVC_FIRST_COURSE 
    0.5, // VAR_SRVC_MAIN_COURSE 
    0.8, // VAR_SRVC_COFFEE 
    0.2, // VAR_SRVC_CHECKOUT 
    0.2, // VAR_SRVC_REFILL 
};

/* ====================== SIM DATA ===================== */
#define DISHES_COUNT 10

#endif
