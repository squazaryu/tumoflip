#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Maximum UTF-8 bytes accepted by the Morse Player text input. */
#define MORSE_PLAYER_TEXT_MAX_BYTES 96U

/** Maximum number of timed tone/silence segments for one text. */
#define MORSE_PLAYER_MAX_SEGMENTS 1536U

/** Maximum bytes used by the human-readable Morse preview. */
#define MORSE_PLAYER_DISPLAY_MAX_BYTES 768U

/** A segment is a duration in Morse units; bit 7 marks an audible tone. */
typedef uint8_t MorsePlayerSegment;

#define MORSE_PLAYER_TONE_FLAG 0x80U
#define MORSE_PLAYER_SEGMENT_UNITS_MASK 0x7FU

typedef struct {
    size_t segment_count;
    uint32_t total_units;
    size_t unknown_count;
} MorsePlayerProgramInfo;

/**
 * Find a Morse pattern for an ASCII/Unicode codepoint.
 *
 * ASCII letters are matched case-insensitively. Cyrillic letters, digits and
 * common punctuation are supported. A NULL result means that the codepoint
 * has no safe representation in the table.
 */
const char* morse_player_pattern_for_codepoint(uint32_t codepoint);

/** Decode one UTF-8 codepoint and advance the input pointer. */
uint32_t morse_player_utf8_next(const char** cursor);

/**
 * Convert text into timed Morse segments.
 *
 * Tone segments have MORSE_PLAYER_TONE_FLAG set; silence segments do not.
 * The function collapses whitespace into a single word gap and reports
 * unsupported codepoints without producing accidental tones for them.
 */
bool morse_player_build_program(
    const char* text,
    MorsePlayerSegment* segments,
    size_t segment_capacity,
    MorsePlayerProgramInfo* info);

/** Build a compact preview such as ".... . .-.. .-.. --- / .--". */
bool morse_player_build_display(
    const char* text,
    char* output,
    size_t output_capacity,
    size_t* unknown_count);
