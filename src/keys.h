#ifndef KEYS_H
#define KEYS_H

/* The keyboard: typed keys from the ROM (GETIN, in ISO mode), and Alt plus
 * a key for busterm's own commands, caught before the ROM types them (see
 * keyhook.s). */

#include <stdint.h>

/* the keyboard's codes, in the X16's ISO mode */
#define KEY_RETURN 0x0D
#define KEY_DEL 0x14    /* backspace */
#define KEY_DELETE 0x19 /* Delete, the key beside Insert */
#define KEY_ESC 0x1B
#define KEY_UP 0x91
#define KEY_DOWN 0x11
#define KEY_LEFT 0x9D
#define KEY_RIGHT 0x1D
#define KEY_HOME 0x13
#define KEY_END 0x04
#define KEY_TAB 0x09

/* busterm's hook on the ROM's keyboard handler, in and out */
void keys_init(void);
void keys_done(void);

/* a key typed, or 0 */
uint8_t keys_get(void);

/* a key pressed with Alt: its letter or digit, lower case ('d' for Alt-D);
 * or Page Up or Down, Alt or not (KEY_PGUP, KEY_PGDN); or 0 */
#define KEY_PGUP 0x80
#define KEY_PGDN 0x81
uint8_t keys_alt(void);

#endif
