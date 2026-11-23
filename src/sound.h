#ifndef SOUND_H
#define SOUND_H

/* A board's bell (BEL, 7): a short beep from VERA's sound generator, its
 * last voice, unless the settings have it silent. */

void bell_ring(void);

/* called often: the beep stopped when it's had its time */
void bell_tick(void);

/* the beep stopped now: as a window opens or a transfer starts, whose
 * loops don't call bell_tick */
void bell_stop(void);

#endif
