#include <assert.h>
#include <stddef.h>
#include <string.h>

#include "../morse_codec.h"

static void test_ascii_program(void) {
    MorsePlayerSegment segments[MORSE_PLAYER_MAX_SEGMENTS];
    MorsePlayerProgramInfo info;

    assert(morse_player_build_program("SOS", segments, MORSE_PLAYER_MAX_SEGMENTS, &info));
    assert(info.segment_count == 17U);
    assert(info.total_units == 27U);
    assert(info.unknown_count == 0U);
    assert((segments[0] & MORSE_PLAYER_TONE_FLAG) != 0U);
    assert((segments[1] & MORSE_PLAYER_TONE_FLAG) == 0U);
    assert((segments[1] & MORSE_PLAYER_SEGMENT_UNITS_MASK) == 1U);
}

static void test_word_gap_and_display(void) {
    MorsePlayerSegment segments[MORSE_PLAYER_MAX_SEGMENTS];
    MorsePlayerProgramInfo info;
    char display[MORSE_PLAYER_DISPLAY_MAX_BYTES];

    assert(morse_player_build_program("E T", segments, MORSE_PLAYER_MAX_SEGMENTS, &info));
    assert(info.total_units == 11U);
    assert(morse_player_build_display("E T", display, sizeof(display), NULL));
    assert(strcmp(display, ". / -") == 0);
}

static void test_cyrillic_and_unknown(void) {
    MorsePlayerSegment segments[MORSE_PLAYER_MAX_SEGMENTS];
    MorsePlayerProgramInfo info;
    char display[MORSE_PLAYER_DISPLAY_MAX_BYTES];
    size_t unknown_count = 0U;

    assert(morse_player_build_program("Привет", segments, MORSE_PLAYER_MAX_SEGMENTS, &info));
    assert(info.unknown_count == 0U);
    assert(morse_player_build_display("Привет🙂", display, sizeof(display), &unknown_count));
    assert(unknown_count == 1U);
}

static void test_invalid_utf8_is_safe(void) {
    const char invalid[] = {(char)0xC3, 'A', '\0'};
    MorsePlayerSegment segments[MORSE_PLAYER_MAX_SEGMENTS];
    MorsePlayerProgramInfo info;

    assert(morse_player_build_program(invalid, segments, MORSE_PLAYER_MAX_SEGMENTS, &info));
    assert(info.unknown_count == 1U);
}

int main(void) {
    test_ascii_program();
    test_word_gap_and_display();
    test_cyrillic_and_unknown();
    test_invalid_utf8_is_safe();
    return 0;
}
