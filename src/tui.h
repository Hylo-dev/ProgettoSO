#ifndef _TUI_H 
#define _TUI_H

#include <stddef.h>
#include <stdlib.h>
#include <termios.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>

#include <sys/ioctl.h> 
#include <unistd.h>

#include "tools.h"

/* ================= TUI STUFF ================= */

typedef struct {
    size_t  rows;
    size_t  cols;
    size_t  size; 
    char   *buff;
} screen;

typedef enum {
    BLACK = 30, RED     = 31, GREEN = 32, YELLOW = 33,
    BLUE  = 34, MAGENTA = 35, CYAN  = 36, WHITE  = 37
} color_t;


static void
s_clear(screen *s){
    // sets all the buffer to ' '
    memset(s->buff, ' ', s->size);

    // sets the last col to be '\n'
    for (size_t y = 0; y < s->rows; y++) {
        s->buff[y * (s->cols + 1) + s->cols] = '\n';
    }
    s->buff[s->size] = '\0'; 
}

static inline screen*
init_screen(
    size_t rows,
    size_t cols
) {
    screen *s = (screen*)zmalloc(sizeof(screen));
    s->rows   = rows;
    s->cols   = cols;
    s->size   = rows * (cols + 1); 
    s->buff   =   (char*)zmalloc(s->size + 1);
    
    s_clear(s);
    
    return s;
}

static inline void
s_display(screen *s) {
    const char *clear_and_home = "\x1b[2J\x1b[H";
    write(STDOUT_FILENO, clear_and_home, 7);
    
    write(STDOUT_FILENO, s->buff, s->size);
    fsync(STDOUT_FILENO);
}

static inline void
emergency_reset() {
    printf("\x1b[?25h\x1b[0m\x1b[2J\x1b[H");
    fflush(stdout);
}

static inline void
free_screen(screen *s) {
    if (s) {
        if (s->buff) free(s->buff);
        free(s);
    }
}

static inline void
s_put(
    screen *s,
    size_t  x,
    size_t  y,
    char    c
) {
    if (x >= s->cols || y >= s->rows)
        return;
    
    size_t idx = (size_t)y * (s->cols + 1) + (size_t)x;
    s->buff[idx] = c;
}

static inline void
s_write_h(
    screen *s,
    size_t  x,
    size_t  y,
    const char* str
) {
    for (size_t i = 0; str[i] != '\0'; i++)
        s_put(s, x + i, y, str[i]);
}

static inline void
s_write_v(
    screen *s,
    size_t  x,
    size_t  y,
    const char* str
) {
    for (size_t i = 0; str[i] != '\0'; i++)
        s_put(s, x, y + i, str[i]);
}

static inline void
s_draw_text_h(
    screen *s,
    size_t  x,
    size_t  y,
    const char* fmt, ...
) {
    char temp_buff[256]; 

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(temp_buff, sizeof(temp_buff), fmt, ap);
    va_end(ap);

    s_write_h(s, x, y, temp_buff);
}

static inline void
s_draw_text_v(
    screen *s,
    size_t  x,
    size_t  y,
    const char* fmt, ...
) {
    char temp_buff[256]; 

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(temp_buff, sizeof(temp_buff), fmt, ap);
    va_end(ap);

    s_write_h(s, x, y, temp_buff);
}

static inline void
s_repeat_h(
    screen *s,
    size_t  x,
    size_t  y,
    char    c,
    size_t  times
) {
    for (size_t i = 0; i < times; i++)
        s_put(s, x+i, y, c);
}

static inline void
s_repeat_v(
    screen *s,
    size_t  x,
    size_t  y,
    char    c,
    size_t  times
) {
    for (size_t i = 0; i < times; i++)
        s_put(s, x, y - i, c);
}

// ================= TERMINAL SETUP =================

static struct termios orig_termios;

static inline void
disableRawMode(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    printf("\x1b[?25h"); // Mostra cursore
}

static inline void
enableRawMode(void) {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disableRawMode);

    struct termios raw = orig_termios;
    
    raw.c_lflag &= ~((tcflag_t)(ECHO | ICANON));
    raw.c_iflag &= ~((tcflag_t)(IXON)); 
    
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    
    printf("\x1b[?25l");
}

static inline void
get_terminal_size(
    size_t *rows,
    size_t *cols
) {
    struct winsize w;
    
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == -1) {
        *rows = 24;
        *cols = 80;
    } else {
        *rows = w.ws_row;
        *cols = w.ws_col;
    }
}


static inline void
set_color(color_t c) {
    printf("\x1b[%dm", c);
}

static inline void
reset_color(void) {
    printf("\x1b[0m");
}


static inline void
clear_screen(void) {
    // clear the screen
    printf("\x1b[2J");
    // move cursor to 0, 0
    printf("\x1b[H");
}

#endif
