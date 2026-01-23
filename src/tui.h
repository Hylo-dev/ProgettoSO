#ifndef _TUI_H 
#define _TUI_H

#include <stddef.h>
#include <stdlib.h>
#include <termios.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <signal.h> // Necessario per gestire CTRL+C

#include <sys/ioctl.h> 
#include <unistd.h>

#include "tools.h" // Assumo contenga zmalloc

#define FULL_BLOCK_CHAR '#' 
#define EMPTY_BLOCK_CHAR '^'

#define UTF8_FULL_BLOCK "\xe2\x96\x88"

// Colori ANSI 
#define ANSI_WHITE "\x1b[37m"
#define ANSI_GRAY  "\x1b[90m"
#define ANSI_RESET "\x1b[0m"
#define ANSI_HOME  "\x1b[H"
#define ANSI_CLEAR "\x1b[2J"
#define ANSI_HIDE_CURSOR "\x1b[?25l"
#define ANSI_SHOW_CURSOR "\x1b[?25h"

/* ================= TUI DATA ================= */

typedef struct {
    size_t  rows;
    size_t  cols;
    size_t  size; 
    char   *buff;
} screen;

static struct termios orig_termios;

/* ================= UTILS & CLEANUP ================= */

static inline void
reset_terminal(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    write(STDOUT_FILENO, ANSI_SHOW_CURSOR, sizeof(ANSI_SHOW_CURSOR) - 1);
    write(STDOUT_FILENO, ANSI_RESET, sizeof(ANSI_RESET) - 1);
    write(STDOUT_FILENO, ANSI_CLEAR, sizeof(ANSI_CLEAR) - 1);
    write(STDOUT_FILENO, ANSI_HOME, sizeof(ANSI_HOME) - 1);
}

// Handler per i segnali (es. CTRL+C)
static void 
handle_signal_tui(int sig) {
    (void)sig; // Unused
    reset_terminal();
    exit(0);
}

static inline void
enableRawMode(void) {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(reset_terminal);
    
    struct sigaction sa;
    sa.sa_handler = handle_signal_tui;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    struct termios raw = orig_termios;
    raw.c_lflag &= ~((tcflag_t)(ECHO | ICANON));
    raw.c_iflag &= ~((tcflag_t)(IXON)); 
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    
    write(STDOUT_FILENO, ANSI_HIDE_CURSOR, sizeof(ANSI_HIDE_CURSOR) - 1);
}

/* ================= SCREEN LOGIC ================= */

static void
s_clear(screen *s){
    memset(s->buff, ' ', s->size);
}

static inline screen*
init_screen(size_t rows, size_t cols) {
    screen *s = (screen*)zmalloc(sizeof(screen));
    s->rows   = rows;
    s->cols   = cols;
    s->size   = rows * cols; 
    s->buff   = (char*)zmalloc(s->size);
    
    s_clear(s);
    return s;
}

static inline void
free_screen(screen *s) {
    if (s) {
        if (s->buff) free(s->buff);
        free(s);
    }
}

/* ================= DRAWING PRIMITIVES ================= */

static inline void
s_put(screen *s, size_t x, size_t y, char c) {
    if (x >= s->cols || y >= s->rows) return;
    s->buff[y * s->cols + x] = c;
}

static inline void
s_write(screen *s, size_t x, size_t y, const char* str) {
    if (y >= s->rows) return;
    for (size_t i = 0; str[i] != '\0'; i++) {
        if (x + i >= s->cols) break;
        s_put(s, x + i, y, str[i]);
    }
}

static inline void
s_write_v(screen *s, size_t x, size_t y, const char* str) {
    if (x >= s->cols) return;
    for (size_t i = 0; str[i] != '\0'; i++) {
        if (y + i >= s->rows) break;
        s_put(s, x, y + i, str[i]);
    }
}

static inline void
s_draw_text(screen *s, size_t x, size_t y, const char* fmt, ...) {
    va_list ap;
    
    va_start(ap, fmt);
    int len = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    
    if (len < 0) return;

    size_t size = (size_t)len + 1;
    char *buf   =  (char*)zmalloc(size);

    va_start(ap, fmt);
    vsnprintf(buf, size, fmt, ap);
    va_end(ap);

    s_write(s, x, y, buf);
    free(buf);
}

static inline void
s_draw_text_v(
    screen *s,
    size_t  x,
    size_t  y,
    const char* fmt, ...
) {
    va_list ap;
    va_start(ap, fmt);
    int len = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);

    if (len < 0) return;

    size_t size = (size_t)len + 1;
    char *buf = (char*)zmalloc(size);

    va_start(ap, fmt);
    vsnprintf(buf, size, fmt, ap);
    va_end(ap);

    s_write_v(s, x, y, buf); 
    free(buf);
}

static inline void
s_repeat(screen *s, size_t x, size_t y, char c, int times) {
    int count = (times < 0) ? -times : times;
    int dir   = (times < 0) ? -1 : 1;

    for (int i = 0; i < count; i++) {
        int curr_x = (int)x + (i * dir);
        if (curr_x >= 0) s_put(s, (size_t)curr_x, y, c);
    }
}

static inline void
s_draw_bar(
    screen *s,
    size_t  x,
    size_t  y,
    int     width,
    float   percentage
) {
    int abs_width = (width < 0) ? -width : width;
    int filled    = (int)((float)abs_width * percentage);
    int dir       = (width < 0) ? -1 : 1;
    
    for (int i = 0; i < abs_width; i++) {
        int cx = (int)x + (i * dir);
        if (cx < 0 || (size_t)cx >= s->cols) continue;

        char c = (i < filled) ? FULL_BLOCK_CHAR : EMPTY_BLOCK_CHAR;
        s_put(s, (size_t)cx, y, c);
    }
}

static inline void
s_draw_bar_v(
    screen *s,
    size_t  x,
    size_t  y,
    int     height,
    float   percentage
) {
    int abs_h  = (height < 0) ? -height : height;
    int filled = (int)((float)abs_h * percentage);
    int dir    = (height < 0) ? -1 : 1;

    for (int i = 0; i < abs_h; i++) {
        int cy = (int)y + (i * dir);
        if (cy < 0 || (size_t)cy >= s->rows) continue;

        char c = (i < filled) ? FULL_BLOCK_CHAR : EMPTY_BLOCK_CHAR;
        
        s_put(s, x, (size_t)cy, c);
        if (x + 1 < s->cols) {
            s_put(s, x + 1, (size_t)cy, c);
        }
    }
}

/* ================= RENDERING (NO FLICKER) ================= */

static inline void
s_display(screen *s) {
    size_t max_render_size = (s->size * 15) + (s->rows * 5) + 64; 
    
    char *out_buf = (char*)zmalloc(max_render_size);
    char *ptr = out_buf;
    
    strcpy(ptr, ANSI_HOME);
    ptr += strlen(ANSI_HOME);

    bool color_changed = false;

    for (size_t y = 0; y < s->rows; y++) {
        for (size_t x = 0; x < s->cols; x++) {
            char c = s->buff[y * s->cols + x];
            switch (c) {
                case FULL_BLOCK_CHAR:
                    ptr += sprintf(ptr, "%s%s", ANSI_WHITE, UTF8_FULL_BLOCK);
                    color_changed = true;
                    break;
                case EMPTY_BLOCK_CHAR:
                    ptr += sprintf(ptr, "%s%s", ANSI_GRAY, UTF8_FULL_BLOCK);
                    color_changed = true;
                    break;
                default:
                    if (color_changed) 
                        ptr += sprintf(ptr, "%s%c", ANSI_RESET, c);
                    else
                        ptr += sprintf(ptr, "%c", c);
                    break;
            }
        }
        ptr += sprintf(ptr, "%s\n", ANSI_RESET);
    }

    write(STDOUT_FILENO, out_buf, (size_t)(ptr - out_buf));
    
    free(out_buf);
}

static inline void
get_terminal_size(
    size_t *rows,
    size_t *cols
) {
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == -1) {
        *rows = 24; *cols = 80;
    } else {
        *rows = w.ws_row; *cols = w.ws_col;
    }
}

static inline char
s_getch() {
    char c = 0;
    if (read(STDIN_FILENO, &c, 1) < 0) return 0;
    return c;
}

#endif
