#include "note_frequency.h"

/* Frequencies for C0..B0 in Hz. The octave is applied by an exact power of
 * two, so notes keep the same equal-tempered relationship across the range. */
static const float note_frequency_chromatic_octave[] = {
    16.3515978f,
    17.3239144f,
    18.3540478f,
    19.4454365f,
    20.6017227f,
    21.8267649f,
    23.1246510f,
    24.4997148f,
    25.9565430f,
    27.5000000f,
    29.1352367f,
    30.8677063f,
};

float note_frequency_from_semitone(uint8_t semitone) {
    const uint8_t note = semitone % 12U;
    const uint8_t octave = semitone / 12U;
    return note_frequency_chromatic_octave[note] * (float)(1UL << octave);
}
