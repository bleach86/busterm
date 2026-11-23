#include "dir.h"

#include <cbm.h>
#include <cx16.h>

#include "banks.h"
#include "files.h"
#include "keys.h"
#include "ui.h"

uint8_t dir_count;
uint8_t recent_count;

/* BUSTERM.DIR has more boards than DIR_MAX: the first are shown, and the
 * file is left as it is, so none of the rest are lost */
static uint8_t dir_over;

/* a line of the files, as it's read or written */
static char line[128];

static const char *const emu_names[] = {"ANSI", "PETSCII", "ASCII"};

/* ---- text ---------------------------------------------------------- */

static uint8_t length(const char *s) {
    uint8_t n = 0;
    while (s[n]) {
        ++n;
    }
    return n;
}

static void copy(char *to, const char *from, uint8_t room) {
    while (*from && --room) {
        *to++ = *from++;
    }
    *to = 0;
}

static uint8_t same(const char *a, const char *b) {
    while (*a && *a == *b) {
        ++a, ++b;
    }
    return *a == *b;
}

static char *put(char *p, const char *s) {
    while (*s) {
        *p++ = *s++;
    }
    return p;
}

static char *put_number(char *p, uint16_t n) {
    char digits[6];
    uint8_t i = 0;
    do {
        digits[i++] = '0' + n % 10;
        n /= 10;
    } while (n);
    while (i) {
        *p++ = digits[--i];
    }
    return p;
}

static void now(char *when) {
    cx16_date_time_t t;
    uint8_t v[5], i;
    cx16_k_clock_get_date_time(&t);
    v[0] = t.year % 100, v[1] = t.mon, v[2] = t.day, v[3] = t.hour, v[4] = t.min;
    for (i = 0; i < 5; ++i) {
        *when++ = '0' + v[i] / 10;
        *when++ = '0' + v[i] % 10;
    }
    *when = 0;
}

/* ---- the files ------------------------------------------------------- */

/* the fields of the line at |p|, split at '|', up to |most|; the line's
 * end returned */
static char *fields(char *p, char **f, uint8_t most, uint8_t *count) {
    uint8_t n = 0;
    f[n++] = p;
    while (*p && *p != '\n' && *p != '\r') {
        if (*p == '|' && n < most) {
            *p = 0;
            f[n++] = p + 1;
        }
        ++p;
    }
    while (*p == '\n' || *p == '\r') {
        *p++ = 0;
    }
    *count = n;
    return p;
}

static uint16_t number(const char *s) {
    uint16_t n = 0;
    while (*s >= '0' && *s <= '9') {
        n = n * 10 + (*s++ - '0');
    }
    return n;
}

static uint8_t emu_of(const char *s) {
    uint8_t i;
    for (i = 0; i < 3; ++i) {
        if (same(s, emu_names[i])) {
            return i;
        }
    }
    return EMU_ANSI;
}

/* The boards a first run starts with, as BUSTERM.DIR has them */
static const char first_boards[] = "Open Bus|bbs.tuxprint.com:8888|ANSI|80\n"
                                   "Open Bus C64|bbs.tuxprint.com:6400|PETSCII|40\n"
                                   "The Oasis BBS|oasisbbs.hopto.org:6400|PETSCII|40\n"
                                   "Borderline BBS|borderlinebbs.dyndns.org:6400|PETSCII|40\n"
                                   "Level 29|bbs.fozztexx.com:23|ANSI|80\n";

/* the board on |line|, a line of BUSTERM.DIR, added */
static void add(void) {
    char *f[6];
    uint8_t n;
    fields(line, f, 6, &n);
    if (n >= 2 && *f[0]) {
        board_t *b;
        if (dir_count == DIR_MAX) {
            dir_over = 1;
            return;
        }
        b = &dir[dir_count++];
        copy(b->name, f[0], sizeof b->name);
        copy(b->addr, f[1], sizeof b->addr);
        b->emu = n > 2 ? emu_of(f[2]) : EMU_ANSI;
        b->cols = n > 3 && number(f[3]) == 40 ? 40 : 80;
        b->calls = n > 4 ? number(f[4]) : 0;
        copy(b->last, n > 5 ? f[5] : "", sizeof b->last);
    }
}

void dir_load(void) {
    dir_count = 0;
    dir_over = 0;
    if (file_open("BUSTERM.DIR,S,R")) {
        while (!dir_over && file_line(line, sizeof line)) {
            add();
        }
        file_close();
    }
    if (!dir_count) {
        /* a first run: boards to start with */
        const char *s = first_boards;
        while (*s) {
            char *p = line;
            while ((*p++ = *s++) != '\n') {
            }
            *p = 0;
            add();
        }
    }

    recent_count = 0;
    if (file_open("BUSTERM.RCT,S,R")) {
        while (recent_count < RECENT_MAX && file_line(line, sizeof line)) {
            char *f[3];
            uint8_t n;
            fields(line, f, 3, &n);
            if (n == 3 && *f[1]) {
                call_t *c = &recent[recent_count++];
                copy(c->name, f[0], sizeof c->name);
                copy(c->addr, f[1], sizeof c->addr);
                copy(c->when, f[2], sizeof c->when);
            }
        }
        file_close();
    }
}

void dir_save(void) {
    char *p;
    uint8_t i;
    if (!dir_over && file_create("@:BUSTERM.DIR,S,W")) {
        for (i = 0; i < dir_count; ++i) {
            board_t *b = &dir[i];
            p = put(line, b->name), *p++ = '|';
            p = put(p, b->addr), *p++ = '|';
            p = put(p, emu_names[b->emu]), *p++ = '|';
            p = put_number(p, b->cols), *p++ = '|';
            p = put_number(p, b->calls), *p++ = '|';
            p = put(p, b->last), *p++ = '\n', *p = 0;
            file_put(line);
        }
        file_close();
    }
    if (file_create("@:BUSTERM.RCT,S,W")) {
        for (i = 0; i < recent_count; ++i) {
            call_t *c = &recent[i];
            p = put(line, c->name), *p++ = '|';
            p = put(p, c->addr), *p++ = '|';
            p = put(p, c->when), *p++ = '\n', *p = 0;
            file_put(line);
        }
        file_close();
    }
}

void dir_called(const char *name, const char *addr) {
    uint8_t i;
    char when[WHEN_LEN + 1];
    call_t call;

    now(when);
    for (i = 0; i < dir_count; ++i) {
        if (same(dir[i].addr, addr)) {
            ++dir[i].calls;
            copy(dir[i].last, when, sizeof dir[i].last);
        }
    }
    /* to the top of the recent calls, once */
    copy(call.name, name, sizeof call.name);
    copy(call.addr, addr, sizeof call.addr);
    copy(call.when, when, sizeof call.when);
    for (i = 0; i < recent_count && !same(recent[i].addr, addr); ++i) {
    }
    if (i == recent_count && recent_count < RECENT_MAX) {
        ++recent_count;
    }
    if (i == RECENT_MAX) {
        i = RECENT_MAX - 1;
    }
    for (; i; --i) {
        recent[i] = recent[i - 1];
    }
    recent[0] = call;
    dir_save();
}

/* ---- the window ------------------------------------------------------ */

#define W_ROW 2
#define W_COL 5
#define W_HEIGHT 24
#define W_WIDTH 70
#define LIST_ROW (W_ROW + 3)
#define LIST_ROWS 17

/* DIR_MAX, as text */
#define TEXT_OF(n) #n
#define NUMBER_TEXT(n) TEXT_OF(n)
#define MAX_TEXT NUMBER_TEXT(DIR_MAX)

#define NAME_COL (W_COL + 3)
#define NAME_W 22
#define ADDR_COL (W_COL + 26)
#define ADDR_W 30
#define TYPE_COL (W_COL + 57)
#define TYPE_W 11
#define WHEN_COL (W_COL + 50)
#define WHEN_W 18
#define RECENT_ADDR_W 22

static uint8_t showing_recent;

/* "20YY-MM-DD hh:mm" from YYMMDDhhmm, or blank */
static void when_text(char *out, const char *when) {
    if (length(when) != WHEN_LEN) {
        out[0] = 0;
        return;
    }
    out[0] = '2', out[1] = '0', out[2] = when[0], out[3] = when[1], out[4] = '-';
    out[5] = when[2], out[6] = when[3], out[7] = '-', out[8] = when[4], out[9] = when[5];
    out[10] = ' ', out[11] = when[6], out[12] = when[7], out[13] = ':', out[14] = when[8];
    out[15] = when[9], out[16] = 0;
}

static void cell_text(uint8_t row, uint8_t col, const char *s, uint8_t width, uint8_t attr) {
    char buf[ADDR_LEN + 1];
    uint8_t n;
    copy(buf, s, width + 1 < sizeof buf ? width + 1 : sizeof buf);
    n = length(buf);
    ui_text(row, col, buf, attr, width - n);
}

static void draw_row(uint8_t i, uint8_t top, uint8_t picked) {
    uint8_t row = LIST_ROW + (i - top);
    uint8_t attr = i == picked ? UI_PICKED : UI_TEXT;
    char type[12];
    ui_fill(row, W_COL + 1, 2, ' ', attr);
    char when[18];
    if (showing_recent) {
        call_t *c = &recent[i];
        cell_text(row, NAME_COL, c->name, NAME_W + 1, attr);
        cell_text(row, ADDR_COL, c->addr, RECENT_ADDR_W + 2, attr);
        when_text(when, c->when);
        cell_text(row, WHEN_COL, when, WHEN_W, attr);
    } else {
        board_t *b = &dir[i];
        char *p = put(type, emu_names[b->emu]);
        *p++ = ' ';
        p = put_number(p, b->cols);
        *p = 0;
        cell_text(row, NAME_COL, b->name, NAME_W + 1, attr);
        cell_text(row, ADDR_COL, b->addr, ADDR_W + 1, attr);
        cell_text(row, TYPE_COL, type, TYPE_W, attr);
    }
    ui_fill(row, W_COL + W_WIDTH - 2, 1, ' ', attr);
}

static void draw_list(uint8_t top, uint8_t picked) {
    uint8_t count = showing_recent ? recent_count : dir_count;
    uint8_t r;
    ui_text(LIST_ROW - 2, W_COL + 1, "", UI_TEXT, W_WIDTH - 2);
    ui_text(LIST_ROW - 1, W_COL + 1, "", UI_TEXT, W_WIDTH - 2);
    if (showing_recent) {
        ui_text(LIST_ROW - 2, NAME_COL, "Recent calls", UI_TITLE, 0);
        ui_text(LIST_ROW - 2, WHEN_COL, "Tab: directory", UI_DIM, 0);
        ui_text(LIST_ROW - 1, NAME_COL, "name", UI_DIM, 0);
        ui_text(LIST_ROW - 1, ADDR_COL, "address", UI_DIM, 0);
        ui_text(LIST_ROW - 1, WHEN_COL, "when", UI_DIM, 0);
    } else {
        ui_text(LIST_ROW - 2, NAME_COL, "Boards", UI_TITLE, 0);
        /* why N, E or Del might do nothing */
        if (dir_over) {
            ui_text(LIST_ROW - 2, NAME_COL + 8, "first " MAX_TEXT " of BUSTERM.DIR: read only",
                    UI_DIM, 0);
        } else if (dir_count == DIR_MAX) {
            ui_text(LIST_ROW - 2, NAME_COL + 8, "full: Del makes room", UI_DIM, 0);
        }
        ui_text(LIST_ROW - 2, WHEN_COL, "Tab: recent calls", UI_DIM, 0);
        ui_text(LIST_ROW - 1, NAME_COL, "name", UI_DIM, 0);
        ui_text(LIST_ROW - 1, ADDR_COL, "address", UI_DIM, 0);
        ui_text(LIST_ROW - 1, TYPE_COL, "type", UI_DIM, 0);
    }
    for (r = 0; r < LIST_ROWS; ++r) {
        if (top + r < count) {
            draw_row(top + r, top, picked);
        } else {
            ui_fill(LIST_ROW + r, W_COL + 1, W_WIDTH - 2, ' ', UI_TEXT);
        }
    }
    if (!count) {
        ui_text(LIST_ROW, NAME_COL, showing_recent ? "No calls yet." : "No boards yet: N adds one.",
                UI_DIM, 0);
    }
}

static const char *const list_hints[] = {
    "Return",    " dial  ", "N",         " new  ", "E",      " edit  ", "Del",
    " delete  ", "Tab",     " recent  ", "Esc",    " modem", 0};
static const char *const recent_hints[] = {"Return", " dial  ", "Tab", " directory  ",
                                           "Esc",    " modem",  0};

static void draw_hints(void) {
    ui_fill(W_ROW + W_HEIGHT - 2, W_COL + 1, W_WIDTH - 2, ' ', UI_TEXT);
    ui_hints(W_ROW + W_HEIGHT - 2, NAME_COL, showing_recent ? recent_hints : list_hints);
}

/* ---- the form for a board -------------------------------------------- */

#define F_ROW 8
#define F_COL 12
#define F_HEIGHT 12
#define F_WIDTH 56
#define F_FIELD (F_COL + 12)

static const char *const form_hints[] = {"Tab", " next  ", "Return", " save  ",
                                         "Esc", " cancel", 0};

static void draw_choice(uint8_t row, const char *const *names, uint8_t count, uint8_t chosen,
                        uint8_t focused) {
    uint8_t col = F_FIELD, i;
    for (i = 0; i < count; ++i) {
        uint8_t attr = i == chosen ? (focused ? UI_PICKED : UI_FIELD) : UI_DIM;
        ui_fill(row, col, 1, ' ', attr);
        ui_text(row, col + 1, names[i], attr, 1);
        col += length(names[i]) + 3;
    }
}

static const char *const col_names[] = {"80", "40"};

/* the form; 1 if saved */
static uint8_t edit_board(board_t *b, const char *title) {
    window_t w;
    board_t e = *b;
    uint8_t field = 0, k = 0;

    if (!ui_open(&w, F_ROW, F_COL, F_HEIGHT, F_WIDTH, title)) {
        return 0;
    }
    ui_text(F_ROW + 2, F_COL + 3, "name", UI_DIM, 0);
    ui_text(F_ROW + 4, F_COL + 3, "address", UI_DIM, 0);
    ui_text(F_ROW + 5, F_COL + 3, "", UI_DIM, 0);
    ui_text(F_ROW + 5, F_FIELD, "host:port, as bbs.example.org:23", UI_DIM, 0);
    ui_text(F_ROW + 7, F_COL + 3, "type", UI_DIM, 0);
    ui_text(F_ROW + 8, F_COL + 3, "columns", UI_DIM, 0);
    ui_hints(F_ROW + F_HEIGHT - 2, F_COL + 3, form_hints);

    for (;;) {
        ui_text(F_ROW + 2, F_FIELD, e.name, UI_FIELD, 30 - length(e.name));
        ui_text(F_ROW + 4, F_FIELD, e.addr, UI_FIELD,
                40 - (length(e.addr) > 40 ? 40 : length(e.addr)));
        draw_choice(F_ROW + 7, emu_names, 3, e.emu, field == 2);
        draw_choice(F_ROW + 8, col_names, 2, e.cols == 40, field == 3);

        if (field == 0) {
            k = ui_field(F_ROW + 2, F_FIELD, 30, e.name, sizeof e.name);
        } else if (field == 1) {
            k = ui_field(F_ROW + 4, F_FIELD, 40, e.addr, sizeof e.addr);
        } else {
            do {
                k = keys_get();
            } while (!k);
            if (k == KEY_LEFT || k == KEY_RIGHT || k == ' ') {
                if (field == 2) {
                    e.emu = k == KEY_LEFT ? (e.emu + 2) % 3 : (e.emu + 1) % 3;
                } else {
                    e.cols = e.cols == 40 ? 80 : 40;
                }
                continue;
            }
        }
        if (k == KEY_ESC) {
            ui_close(&w);
            return 0;
        }
        if (k == KEY_RETURN) {
            if (*e.name && *e.addr) {
                *b = e;
                ui_close(&w);
                return 1;
            }
            field = *e.name ? 1 : 0;
        } else if (k == KEY_UP) {
            field = (field + 3) % 4;
        } else if (k == KEY_TAB || k == KEY_DOWN) {
            field = (field + 1) % 4;
        }
    }
}

/* ---- the directory --------------------------------------------------- */

/* a recent call as a board: its board's, if it's in the directory */
static board_t called;

static const board_t *board_for(const call_t *c) {
    uint8_t i;
    for (i = 0; i < dir_count; ++i) {
        if (same(dir[i].addr, c->addr)) {
            return &dir[i];
        }
    }
    copy(called.name, c->name, sizeof called.name);
    copy(called.addr, c->addr, sizeof called.addr);
    called.emu = EMU_ANSI, called.cols = 80;
    return &called;
}

/* where the directory was left, to open there again */
static uint8_t picked, top;

const board_t *dir_window(void) {
    window_t w;
    uint8_t count = showing_recent ? recent_count : dir_count;
    if (picked >= count) {
        picked = count ? count - 1 : 0;
    }
    if (picked < top || picked >= top + LIST_ROWS) {
        top = picked;
    }

    if (!ui_open(&w, W_ROW, W_COL, W_HEIGHT, W_WIDTH, "Dialling Directory")) {
        return 0;
    }
    draw_hints();
    draw_list(top, picked);

    for (;;) {
        uint8_t count = showing_recent ? recent_count : dir_count;
        /* boards to add, edit and delete: not if BUSTERM.DIR has too many */
        uint8_t editing = !showing_recent && !dir_over;
        uint8_t k;
        int16_t to;
        do {
            /* Page Up and Down come from busterm's key hook, as Alt's do */
            k = keys_alt();
            if (k != KEY_PGUP && k != KEY_PGDN) {
                k = keys_get();
            }
        } while (!k);

        /* a move: the row it's to, kept in the list */
        to = picked;
        if (k == KEY_UP) {
            --to;
        } else if (k == KEY_DOWN) {
            ++to;
        } else if (k == KEY_PGUP) {
            to -= LIST_ROWS;
        } else if (k == KEY_PGDN) {
            to += LIST_ROWS;
        } else if (k == KEY_HOME) {
            to = 0;
        } else if (k == KEY_END) {
            to = DIR_MAX;
        }
        if (to >= count) {
            to = count - 1;
        }
        if (to < 0) {
            to = 0;
        }

        if (to != picked) {
            picked = to;
        } else if (k == KEY_TAB) {
            showing_recent = !showing_recent;
            picked = 0;
            draw_hints();
        } else if (k == KEY_ESC) {
            ui_close(&w);
            return 0;
        } else if (k == KEY_RETURN && count) {
            ui_close(&w);
            return showing_recent ? board_for(&recent[picked]) : &dir[picked];
        } else if (editing && (k == 'n' || k == 'N') && dir_count < DIR_MAX) {
            board_t b = {"", "", EMU_ANSI, 80, 0, ""};
            if (edit_board(&b, "New board")) {
                dir[dir_count] = b;
                picked = dir_count++;
                dir_save();
            }
        } else if (editing && (k == 'e' || k == 'E') && count) {
            if (edit_board(&dir[picked], "Edit board")) {
                dir_save();
            }
        } else if (editing && (k == KEY_DELETE || k == KEY_DEL) && count) {
            uint8_t i;
            if (!ui_ask("Delete this board?", dir[picked].name)) {
                continue;
            }
            for (i = picked; i + 1 < dir_count; ++i) {
                dir[i] = dir[i + 1];
            }
            --dir_count;
            if (picked && picked >= dir_count) {
                --picked;
            }
            dir_save();
        } else {
            continue;
        }

        if (picked < top) {
            top = picked;
        } else if (picked >= top + LIST_ROWS) {
            top = picked - LIST_ROWS + 1;
        }
        draw_list(top, picked);
    }
}
