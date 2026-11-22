#include "keys.h"

#include <cbm.h>

#include "vera.h"

/* the ROM's keyboard handler vector */
#define KEYHDL (*(volatile uint16_t *)0x032E)

extern void busterm_key_hook(void);

/* shared with keyhook.s */
volatile uint8_t key_alt_down;
volatile uint8_t key_alt_code;
volatile uint16_t key_old_hook;

/* the X16's key numbers (the IBM PC's) for the letters A to Z, then the
 * digits 1 to 9 and 0 */
static const uint8_t letter_keys[26] = {31, 50, 48, 33, 19, 34, 35, 36, 24, 37, 38, 39, 52,
                                        51, 25, 26, 17, 20, 32, 21, 23, 49, 18, 47, 22, 46};
#define KEYNUM_1 2
#define KEYNUM_0 11
#define KEYNUM_PGUP 85
#define KEYNUM_PGDN 86

void keys_init(void) {
    irq_off();
    key_old_hook = KEYHDL;
    KEYHDL = (uint16_t)busterm_key_hook;
    irq_on();
}

void keys_done(void) {
    irq_off();
    KEYHDL = key_old_hook;
    irq_on();
}

uint8_t keys_get(void) { return cbm_k_getin(); }

uint8_t keys_alt(void) {
    uint8_t n, i;
    irq_off();
    n = key_alt_code;
    key_alt_code = 0;
    irq_on();
    if (!n) {
        return 0;
    }
    for (i = 0; i < 26; ++i) {
        if (letter_keys[i] == n) {
            return 0x61 + i; /* a */
        }
    }
    if (n >= KEYNUM_1 && n < KEYNUM_0) {
        return 0x31 + n - KEYNUM_1; /* 1 to 9 */
    }
    if (n == KEYNUM_0) {
        return 0x30;
    }
    if (n == KEYNUM_PGUP) {
        return KEY_PGUP;
    }
    if (n == KEYNUM_PGDN) {
        return KEY_PGDN;
    }
    return 0;
}
