#ifndef SETTINGS_H
#define SETTINGS_H

/* busterm's settings, kept on the SD card in BUSTERM.CFG, a line each:
 *
 *   start=directory      or modem: what busterm opens on
 *   backspace=bs         or del: what Backspace sends a board
 *   echo=off             or on: what's typed shown here, for boards that
 *                        don't echo it
 *   cursor=steady        or blink
 *   bell=beep            or silent: a board's BEL
 *   clock=24             or 12: the status line's clock
 *
 * Each is 0 for the first choice, 1 for the second. */

#include <stdint.h>

enum { SET_START, SET_BACKSPACE, SET_ECHO, SET_CURSOR, SET_BELL, SET_CLOCK, SETTINGS };

extern uint8_t settings[SETTINGS];

void settings_load(void);
void settings_save(void);

/* the settings' window: changed and saved, or left as they were */
void settings_window(void);

#endif
