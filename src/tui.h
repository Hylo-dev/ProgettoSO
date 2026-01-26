#ifndef _TUI_H
#define _TUI_H

#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <wchar.h>

#include "tools.h"

/* ================= COSTANTI E DEFINIZIONI ================= */

// Codepoint Unicode utili
#define CODEPOINT_SPACE 0x0020
#define CODEPOINT_FULL_BLOCK 0x2588 // █


// Helper per stringhe Unicode (Box Drawing)
#define BOX_H  "\u2500" /* ─ */
#define BOX_V  "\u2502" /* │ */
#define BOX_TL "\u250C" /* ┌ */
#define BOX_TR "\u2510" /* ┐ */
#define BOX_BL "\u2514" /* └ */
#define BOX_BR "\u2518" /* ┘ */
#define BOX_VR "\u251C" /* ├ */
#define BOX_VL "\u2524" /* ┤ */
#define BOX_HD "\u252C" /* ┬ */
#define BOX_HU "\u2534" /* ┴ */
#define BOX_HV "\u253C" /* ┼ */


enum {
    COL_RESET = 0,
    COL_WHITE,
    COL_GRAY,
    COL_RED,
    COL_GREEN
};

static const char *ANSI_COLORS[] = {
    "\x1b[0m",  // COL_RESET
    "\x1b[37m", // COL_WHITE
    "\x1b[90m", // COL_GRAY
    "\x1b[31m", // COL_RED 
    "\x1b[32m", // COL_GREEN 
};

#define ANSI_HOME "\x1b[H"
#define ANSI_CLEAR "\x1b[2J"
#define ANSI_HIDE_CURSOR "\x1b[?25l"
#define ANSI_SHOW_CURSOR "\x1b[?25h"

/* ================= STRUTTURE DATI OTTIMIZZATE ================= */

typedef struct {
    uint32_t ch;
    uint8_t  color;
} Cell;

typedef struct {
    size_t rows;
    size_t cols;
    size_t len;
    Cell  *cells;
} screen;

static struct termios orig_termios;

/* ================= UTF-8 HELPERS (INLINE) ================= */

static inline int
utf8_decode(const char *s, uint32_t *out) {
    if (!s || !*s)
        return 0;
    unsigned char c = (unsigned char)s[0];

    if (c < 0x80) {
        *out = c;
        return 1;
    }
    if ((c & 0xE0) == 0xC0) {
        *out = ((c & 0x1F) << 6) | (s[1] & 0x3F);
        return 2;
    }
    if ((c & 0xF0) == 0xE0) {
        *out = ((c & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        return 3;
    }
    if ((c & 0xF8) == 0xF0) {
        *out = ((c & 0x07) << 18) | ((s[1] & 0x3F) << 12) |
               ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
        return 4;
    }
    *out = '?'; // Fallback per byte non validi
    return 1;
}

static inline int
utf8_encode(char *out, uint32_t utf) {
    if (utf <= 0x7F) {
        out[0] = (char)utf;
        return 1;
    } else if (utf <= 0x7FF) {
        out[0] = (char)(0xC0 | (utf >> 6));
        out[1] = (char)(0x80 | (utf & 0x3F));
        return 2;
    } else if (utf <= 0xFFFF) {
        out[0] = (char)(0xE0 | (utf >> 12));
        out[1] = (char)(0x80 | ((utf >> 6) & 0x3F));
        out[2] = (char)(0x80 | (utf & 0x3F));
        return 3;
    } else {
        out[0] = (char)(0xF0 | (utf >> 18));
        out[1] = (char)(0x80 | ((utf >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((utf >> 6) & 0x3F));
        out[3] = (char)(0x80 | (utf & 0x3F));
        return 4;
    }
}

/* ================= SETUP TERMINALE ================= */

static inline void
reset_terminal(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    write(STDOUT_FILENO, ANSI_SHOW_CURSOR, sizeof(ANSI_SHOW_CURSOR) - 1);
    write(
        STDOUT_FILENO, ANSI_COLORS[COL_RESET], strlen(ANSI_COLORS[COL_RESET])
    );
    write(STDOUT_FILENO, ANSI_CLEAR, sizeof(ANSI_CLEAR) - 1);
    write(STDOUT_FILENO, ANSI_HOME, sizeof(ANSI_HOME) - 1);
}

static void
handle_signal_tui(int sig) {
    (void)sig;
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
    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 0;

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    write(STDOUT_FILENO, ANSI_HIDE_CURSOR, sizeof(ANSI_HIDE_CURSOR) - 1);
}

static inline void
get_terminal_size(size_t *rows, size_t *cols) {
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == -1) {
        *rows = 24;
        *cols = 80;
    } else {
        *rows = w.ws_row;
        *cols = w.ws_col;
    }
}

/* ================= GESTIONE SCREEN ================= */

static inline void
s_clear(screen *s) {
    for (size_t i = 0; i < s->len; i++) {
        s->cells[i].ch    = CODEPOINT_SPACE;
        s->cells[i].color = COL_RESET;
    }
}

static inline screen *
init_screen(size_t rows, size_t cols) {
    screen *s = (screen *)zmalloc(sizeof(screen));
    s->rows   = rows;
    s->cols   = cols;
    s->len    = rows * cols;
    s->cells = (Cell *)zmalloc(s->len * sizeof(Cell));

    s_clear(s);
    return s;
}

static inline void
free_screen(screen *s) {
    if (s) {
        if (s->cells)
            free(s->cells);
        free(s);
    }
}

/* ================= DRAWING PRIMITIVES ================= */

static inline void
s_put(screen *s, size_t x, size_t y, uint32_t ch, uint8_t color) {
    if (x >= s->cols || y >= s->rows)
        return;
    size_t idx          = y * s->cols + x;
    s->cells[idx].ch    = ch;
    s->cells[idx].color = color;
}

static inline void
s_write(screen *s, size_t x, size_t y, const char *str, uint8_t color) {
    if (y >= s->rows)
        return;

    size_t      cx  = x;
    const char *ptr = str;

    while (*ptr) {
        if (cx >= s->cols)
            break;

        uint32_t codepoint;
        int      bytes = utf8_decode(ptr, &codepoint);

        s_put(s, cx, y, codepoint, color);

        ptr += bytes;
        cx++;
    }
}

static inline void
s_write_v(screen *s, size_t x, size_t y, const char *str, uint8_t color) {
    if (x >= s->cols)
        return;

    size_t      cy  = y;
    const char *ptr = str;

    while (*ptr) {
        if (cy >= s->rows)
            break;

        uint32_t codepoint;
        int      bytes = utf8_decode(ptr, &codepoint);

        s_put(s, x, cy, codepoint, color);

        ptr += bytes;
        cy++;
    }
}

static inline void
s_draw_text(
    screen *s, size_t x, size_t y, uint8_t color, const char *fmt, ...
) {
    va_list ap;
    va_start(ap, fmt);
    int len = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);

    if (len < 0)
        return;

    size_t size = (size_t)len + 1;
    char  *buf  = (char *)zmalloc(size);

    va_start(ap, fmt);
    vsnprintf(buf, size, fmt, ap);
    va_end(ap);

    s_write(s, x, y, buf, color);
    free(buf);
}

static inline void
s_draw_bar(
    screen *s,
    size_t  x,
    size_t  y,
    int     width,
    float   percentage,
    uint8_t fill_col,
    uint8_t empty_col
) {
    int abs_width = (width < 0) ? -width : width;
    int filled    = (int)((float)abs_width * percentage);
    int dir       = (width < 0) ? -1 : 1;

    for (int i = 0; i < abs_width; i++) {
        int cx = (int)x + (i * dir);
        if (cx < 0 || (size_t)cx >= s->cols)
            continue;

        if (i < filled) {
            s_put(s, (size_t)cx, y, CODEPOINT_FULL_BLOCK, fill_col);
        } else {
            s_put(s, (size_t)cx, y, CODEPOINT_FULL_BLOCK, empty_col);
        }
    }
}

static inline void
s_draw_bar_v(
    screen *s,
    size_t  x,
    size_t  y,
    int     height,
    float   percentage,
    uint8_t fill_col,
    uint8_t empty_col
) {
    int abs_h  = (height < 0) ? -height : height;
    int filled = (int)((float)abs_h * percentage);
    int dir    = (height < 0) ? -1 : 1;

    for (int i = 0; i < abs_h; i++) {
        int cy = (int)y + (i * dir);

        if (cy < 0 || (size_t)cy >= s->rows)
            continue;

        uint8_t color = (i < filled) ? fill_col : empty_col;

        s_put(s, x, (size_t)cy, CODEPOINT_FULL_BLOCK, color);
    }
}


// Helper per disegnare una linea orizzontale
static void
draw_hline(screen *s, size_t x, size_t y, size_t w, uint8_t color) {
    for (size_t i = 0; i < w; i++)
        s_draw_text(s, x + i, y, color, BOX_H);
}

// Helper per disegnare una linea verticale
static void
draw_vline(screen *s, size_t x, size_t y, size_t h, uint8_t color) {
    for (size_t i = 0; i < h; i++)
        s_draw_text(s, x, y + i, color, BOX_V);
}

// Helper per disegnare un box (cornice)
static void
draw_box(screen *s, size_t x, size_t y, size_t w, size_t h, uint8_t color) {
    // Angoli
    s_draw_text(s, x, y, color, BOX_TL);
    s_draw_text(s, x + w - 1, y, color, BOX_TR);
    s_draw_text(s, x, y + h - 1, color, BOX_BL);
    s_draw_text(s, x + w - 1, y + h - 1, color, BOX_BR);

    // Bordi
    draw_hline(s, x + 1, y, w - 2, color);
    draw_hline(s, x + 1, y + h - 1, w - 2, color);
    draw_vline(s, x, y + 1, h - 2, color);
    draw_vline(s, x + w - 1, y + 1, h - 2, color);
}

/* ================= RENDERING ENGINE ================= */

static inline void
s_display(screen *s) {
    size_t max_render_size = (s->len * 12) + 128;

    char *out_buf = (char *)zmalloc(max_render_size);
    char *ptr     = out_buf;

    memcpy(ptr, ANSI_HOME, sizeof(ANSI_HOME) - 1);
    ptr += sizeof(ANSI_HOME) - 1;

    uint8_t last_color = COL_RESET;

    const char *col_str = ANSI_COLORS[COL_RESET];
    size_t      col_len = strlen(col_str);
    memcpy(ptr, col_str, col_len);
    ptr += col_len;

    for (size_t y = 0; y < s->rows; y++) {
        for (size_t x = 0; x < s->cols; x++) {
            Cell *c = &s->cells[y * s->cols + x];

            if (c->color != last_color) {
                const char *new_col_str = ANSI_COLORS[c->color];
                size_t      len         = strlen(new_col_str);

                memcpy(ptr, new_col_str, len);
                ptr += len;
                last_color = c->color;
            }

            ptr += utf8_encode(ptr, c->ch);
        }
        if (y < s->rows - 1)
            *ptr++ = '\n'; 
    }

    memcpy(ptr, ANSI_COLORS[COL_RESET], strlen(ANSI_COLORS[COL_RESET]));
    ptr += strlen(ANSI_COLORS[COL_RESET]);

    write(STDOUT_FILENO, out_buf, (size_t)(ptr - out_buf));

    free(out_buf);
}

static inline char
s_getch() {
    char c = 0;
    if (read(STDIN_FILENO, &c, 1) < 0)
        return 0;
    return c;
}

#endif
