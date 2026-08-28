#pragma once

#include <stdint.h>

/**
 * Return the equal-tempered frequency for a MIDI-style semitone number.
 *
 * The lookup stores one chromatic octave and applies the octave step as a
 * power of two. Callers can therefore share one deterministic conversion
 * without duplicating the exponentiation formula.
 */
float note_frequency_from_semitone(uint8_t semitone);
