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

#define FULL_BLOCK_CHAR '#' 
#define EMPTY_BLOCK_CHAR '^'

#define UTF8_FULL_BLOCK "\xe2\x96\x88"

// Colori ANSI 
#define ANSI_WHITE "\x1b[37m"
#define ANSI_GRAY  "\x1b[90m"
#define ANSI_RESET "\x1b[0m"

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
s_write(
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
s_draw_text(
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

    s_write(s, x, y, temp_buff);
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

    s_write(s, x, y, temp_buff);
}

static inline void
s_repeat(
    screen *s,
    size_t  x,
    size_t  y,
    char    c,
    int     times
) {
    if (times<0) {
        times = abs(times);
        for (size_t i = 0; i < times; i++)
            s_put(s, x-i, y, c);
    } else {
        for (size_t i = 0; i < times; i++)
            s_put(s, x+i, y, c);
    }

}

static inline void
s_repeat_v(
    screen *s,
    size_t  x,
    size_t  y,
    char    c,
    int  times
) {
    if (times<0) {
        times = abs(times);
        for (size_t i = 0; i < times; i++)
            s_put(s, x, y - i, c);
    } else {
        for (size_t i = 0; i < times; i++)
            s_put(s, x, y + i, c);
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
    int filled_units = (int)((float)abs_width * percentage);
    
    for (int i = 0; i < abs_width; i++) {
        int current_x = (width > 0) ? (x + i) : (x - i);
        
        if (current_x < 0 || (size_t)current_x >= s->cols) continue;

        if (i < filled_units)
            s_put(s, (size_t)current_x, (size_t)y, FULL_BLOCK_CHAR); 
        else 
            s_put(s, (size_t)current_x, (size_t)y, EMPTY_BLOCK_CHAR); 
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
    int abs_height = (height < 0) ? -height : height;
    int filled_units = (int)((float)abs_height * percentage);
    
    for (int i = 0; i < abs_height; i++) {
        // CORREZIONE: Usa 'y' come base e 'rows' per il limite
        int current_y = (height > 0) ? (y + i) : (y - i);
        
        if (current_y < 0 || (size_t)current_y >= s->rows) continue;

        if (i < filled_units) {
            s_put(s, x, (size_t)current_y, FULL_BLOCK_CHAR);
            s_put(s, x + 1, (size_t)current_y, FULL_BLOCK_CHAR);
        } else { 
            s_put(s, x, (size_t)current_y, EMPTY_BLOCK_CHAR);
            s_put(s, x + 1, (size_t)current_y, EMPTY_BLOCK_CHAR);
        }
    }
}

// ================= TERMINAL SETUP =================

static struct termios orig_termios;


static inline char
s_getch() {
    char c = 0;
    if (read(STDIN_FILENO, &c, 1) < 0) return 0;
    return c;
}

static inline void
s_display(screen *s) {
    static char out_buf[256000]; 
    char *ptr = out_buf;
    size_t rem = sizeof(out_buf);
    int written;

    written = snprintf(ptr, rem, "\x1b[H");
    if (written > 0) {
        ptr += written;
        rem -= (size_t)written;
    }

    for (size_t i = 0; i < s->size; i++) {
        if (rem < 32) break; 
        char c = s->buff[i];
        switch (c) {
            case FULL_BLOCK_CHAR:
                written = snprintf(ptr, rem, ANSI_WHITE UTF8_FULL_BLOCK);
                break;
            case EMPTY_BLOCK_CHAR:
                written = snprintf(ptr, rem, ANSI_GRAY UTF8_FULL_BLOCK);
                break;
            case '\n':
                written = snprintf(ptr, rem, ANSI_RESET "\n");
                break;
            default:
                written = snprintf(ptr, rem, ANSI_RESET "%c", c);
                break;
        }
        if (written > 0) {
            ptr += written;
            rem -= (size_t)written;
        }
    }

    written = snprintf(ptr, rem, ANSI_RESET);
    if (written > 0) { ptr += written; }
    write(STDOUT_FILENO, out_buf, (size_t)(ptr - out_buf));
}

static inline void
emergency_reset() {
    printf("\x1b[?25h\x1b[0m\x1b[2J\x1b[H");
    fflush(stdout);
}

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
