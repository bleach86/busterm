#include "sound.h"

#include <cx16.h>
#include <stdint.h>

#include "screen.h"
#include "settings.h"
#include "vera.h"

/* VERA's PSG: 16 voices of 4 bytes from $1F9C0, each its frequency (low,
 * high), both sides and the volume (bits 7 and 6, 5 to 0), and its
 * waveform and pulse width (bits 7 and 6, 5 to 0). busterm's is the last. */
#define VOICE 0x1F9FCUL

/* 880 Hz: the frequency word is Hz * 2^17 / 48828.125 */
#define BEEP_FREQ 2362
#define BEEP_VOLUME (0xC0 | 40) /* both sides */
#define SQUARE 0x3F             /* pulse, half the time */
#define BEEP_JIFFIES 8

static uint8_t sounding;
static unsigned long stop_at;

static void voice(uint16_t freq, uint8_t volume) {
    vera_port1(VOICE, VERA_INC1);
    VERA_DATA1 = (uint8_t)freq;
    VERA_DATA1 = (uint8_t)(freq >> 8);
    VERA_DATA1 = volume;
    VERA_DATA1 = SQUARE;
    /* port 1 was moved: what's typed next must put it back, or it lands
     * in the palette, just past the voice */
    screen_forget();
}

void bell_ring(void) {
    if (settings[SET_BELL]) {
        return;
    }
    voice(BEEP_FREQ, BEEP_VOLUME);
    sounding = 1;
    stop_at = cx16_k_rdtim() + BEEP_JIFFIES;
}

void bell_stop(void) {
    if (sounding) {
        voice(BEEP_FREQ, 0);
        sounding = 0;
    }
}

void bell_tick(void) {
    if (sounding && cx16_k_rdtim() >= stop_at) {
        bell_stop();
    }
}
