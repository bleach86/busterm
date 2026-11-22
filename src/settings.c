#include "settings.h"

#include "files.h"
#include "keys.h"
#include "ui.h"

uint8_t settings[SETTINGS];

/* each setting's name in the file, and its two choices', in the file and
 * on the screen */
static const char *const keys_of[SETTINGS] = {"start",  "backspace", "echo",
                                              "cursor", "bell",      "clock"};
static const char *const words[SETTINGS][2] = {
    {"directory", "modem"}, {"bs", "del"},      {"off", "on"},
    {"steady", "blink"},    {"beep", "silent"}, {"24", "12"},
};
static const char *const labels[SETTINGS] = {"start in", "Backspace sends", "local echo",
                                             "cursor",   "a board's bell",  "clock"};
static const char *const shown[SETTINGS][2] = {
    {"directory", "modem prompt"}, {"BS (Ctrl-H)", "DEL"}, {"off", "on"},
    {"steady", "blinking"},        {"beep", "silent"},     {"24 hour", "12 hour"},
};

static uint8_t length(const char *s) {
    uint8_t n = 0;
    while (s[n]) {
        ++n;
    }
    return n;
}

/* |s| starts with |word|, then |end| */
static uint8_t is(const char *s, const char *word, char end) {
    while (*word && *s == *word) {
        ++s, ++word;
    }
    return !*word && *s == end;
}

void settings_load(void) {
    char line[40];
    uint8_t i;

    if (!file_open("BUSTERM.CFG,S,R")) {
        return;
    }
    while (file_line(line, sizeof line)) {
        for (i = 0; i < SETTINGS; ++i) {
            if (is(line, keys_of[i], '=')) {
                const char *v = line + length(keys_of[i]) + 1;
                if (is(v, words[i][0], 0)) {
                    settings[i] = 0;
                } else if (is(v, words[i][1], 0)) {
                    settings[i] = 1;
                }
            }
        }
    }
    file_close();
}

void settings_save(void) {
    uint8_t i;
    if (!file_create("@:BUSTERM.CFG,S,W")) {
        return;
    }
    for (i = 0; i < SETTINGS; ++i) {
        file_put(keys_of[i]);
        file_put("=");
        file_put(words[i][settings[i]]);
        file_put("\n");
    }
    file_close();
}

#define S_ROW 6
#define S_COL 10
#define S_HEIGHT 17
#define S_WIDTH 60
#define LABEL_COL (S_COL + 3)
#define CHOICE_COL (S_COL + 21)

static const char *const hints[] = {
    "Up/Down", " pick  ", "Left/Right", " change  ", "Return", " save  ", "Esc", " cancel", 0};

static void draw(const uint8_t *v, uint8_t focus) {
    uint8_t i, j, col;
    for (i = 0; i < SETTINGS; ++i) {
        uint8_t row = S_ROW + 2 + i * 2;
        ui_text(row, LABEL_COL, labels[i], i == focus ? UI_TITLE : UI_DIM, 0);
        col = CHOICE_COL;
        for (j = 0; j < 2; ++j) {
            uint8_t attr = j == v[i] ? (i == focus ? UI_PICKED : UI_FIELD) : UI_DIM;
            ui_fill(row, col, 1, ' ', attr);
            ui_text(row, col + 1, shown[i][j], attr, 1);
            col += length(shown[i][j]) + 4;
        }
    }
}

void settings_window(void) {
    window_t w;
    uint8_t v[SETTINGS], focus = 0, i;

    for (i = 0; i < SETTINGS; ++i) {
        v[i] = settings[i];
    }
    if (!ui_open(&w, S_ROW, S_COL, S_HEIGHT, S_WIDTH, "Settings")) {
        return;
    }
    ui_hints(S_ROW + S_HEIGHT - 2, LABEL_COL, hints);
    for (;;) {
        uint8_t k;
        draw(v, focus);
        do {
            k = keys_get();
        } while (!k);
        if (k == KEY_UP) {
            focus = focus ? focus - 1 : SETTINGS - 1;
        } else if (k == KEY_DOWN || k == KEY_TAB) {
            focus = (focus + 1) % SETTINGS;
        } else if (k == KEY_LEFT || k == KEY_RIGHT || k == ' ') {
            v[focus] ^= 1;
        } else if (k == KEY_RETURN) {
            for (i = 0; i < SETTINGS; ++i) {
                settings[i] = v[i];
            }
            settings_save();
            break;
        } else if (k == KEY_ESC) {
            break;
        }
    }
    ui_close(&w);
}
