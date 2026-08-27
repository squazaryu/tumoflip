#include "morse_codec.h"

#include <ctype.h>
#include <string.h>

typedef struct {
    uint32_t codepoint;
    const char* pattern;
} MorsePlayerEntry;

/*
 * The table intentionally contains only stable International Morse symbols
 * plus the conventional Russian Cyrillic mapping. Keeping it static makes
 * encoding deterministic and avoids allocating a dictionary at runtime.
 */
static const MorsePlayerEntry morse_player_table[] = {
    {'A', ".-"},
    {'B', "-..."},
    {'C', "-.-."},
    {'D', "-.."},
    {'E', "."},
    {'F', "..-."},
    {'G', "--."},
    {'H', "...."},
    {'I', ".."},
    {'J', ".---"},
    {'K', "-.-"},
    {'L', ".-.."},
    {'M', "--"},
    {'N', "-."},
    {'O', "---"},
    {'P', ".--."},
    {'Q', "--.-"},
    {'R', ".-."},
    {'S', "..."},
    {'T', "-"},
    {'U', "..-"},
    {'V', "...-"},
    {'W', ".--"},
    {'X', "-..-"},
    {'Y', "-.--"},
    {'Z', "--.."},
    {'0', "-----"},
    {'1', ".----"},
    {'2', "..---"},
    {'3', "...--"},
    {'4', "....-"},
    {'5', "....."},
    {'6', "-...."},
    {'7', "--..."},
    {'8', "---.."},
    {'9', "----."},
    {'.', ".-.-.-"},
    {',', "--..--"},
    {'?', "..--.."},
    {'!', "-.-.--"},
    {'\'', ".----."},
    {'/', "-..-."},
    {'(', "-.--."},
    {')', "-.--.-"},
    {'&', ".-..."},
    {':', "---..."},
    {';', "-.-.-."},
    {'=', "-...-"},
    {'+', ".-.-."},
    {'-', "-....-"},
    {'_', "..--.-"},
    {'"', ".-..-."},
    {'$', "...-..-"},
    {'@', ".--.-."},
    {0x0410U, ".-"},    /* А */
    {0x0411U, "-..."},  /* Б */
    {0x0412U, ".--"},   /* В */
    {0x0413U, "--."},   /* Г */
    {0x0414U, "-.."},   /* Д */
    {0x0415U, "."},     /* Е */
    {0x0401U, "."},     /* Ё */
    {0x0416U, "...-"},  /* Ж */
    {0x0417U, "--.."},  /* З */
    {0x0418U, ".."},    /* И */
    {0x0419U, ".---"},  /* Й */
    {0x041AU, "-.-"},   /* К */
    {0x041BU, ".-.."},  /* Л */
    {0x041CU, "--"},    /* М */
    {0x041DU, "-."},    /* Н */
    {0x041EU, "---"},   /* О */
    {0x041FU, ".--."},  /* П */
    {0x0420U, ".-."},   /* Р */
    {0x0421U, "..."},   /* С */
    {0x0422U, "-"},     /* Т */
    {0x0423U, "..-"},   /* У */
    {0x0424U, "..-."},  /* Ф */
    {0x0425U, "...."},  /* Х */
    {0x0426U, "-.-."},  /* Ц */
    {0x0427U, "---."},  /* Ч */
    {0x0428U, "----"},  /* Ш */
    {0x0429U, "--.-"},  /* Щ */
    {0x042AU, ".--.-"}, /* Ъ */
    {0x042BU, "-.--"},  /* Ы */
    {0x042CU, "-..-"},  /* Ь */
    {0x042DU, "..-.."}, /* Э */
    {0x042EU, "..--"},  /* Ю */
    {0x042FU, ".-.-"},  /* Я */
};

static uint32_t morse_player_uppercase_codepoint(uint32_t codepoint) {
    if(codepoint >= (uint32_t)'a' && codepoint <= (uint32_t)'z') {
        return codepoint - ((uint32_t)'a' - (uint32_t)'A');
    }

    /* Russian lowercase letters are a contiguous Unicode block, with Ё
       represented separately. */
    if(codepoint >= 0x0430U && codepoint <= 0x044FU) {
        return codepoint - 0x20U;
    }
    if(codepoint == 0x0451U) return 0x0401U;

    return codepoint;
}

const char* morse_player_pattern_for_codepoint(uint32_t codepoint) {
    codepoint = morse_player_uppercase_codepoint(codepoint);

    for(size_t i = 0; i < sizeof(morse_player_table) / sizeof(morse_player_table[0]); i++) {
        if(morse_player_table[i].codepoint == codepoint) {
            return morse_player_table[i].pattern;
        }
    }

    return NULL;
}

uint32_t morse_player_utf8_next(const char** cursor) {
    const uint8_t* bytes = (const uint8_t*)*cursor;
    uint32_t codepoint;

    if(bytes[0] == '\0') return 0;

    if(bytes[0] < 0x80U) {
        codepoint = bytes[0];
        *cursor += 1;
    } else if((bytes[0] & 0xE0U) == 0xC0U && (bytes[1] & 0xC0U) == 0x80U) {
        codepoint = ((uint32_t)(bytes[0] & 0x1FU) << 6) | (bytes[1] & 0x3FU);
        *cursor += 2;
        if(codepoint < 0x80U) codepoint = 0xFFFDU;
    } else if(
        (bytes[0] & 0xF0U) == 0xE0U && (bytes[1] & 0xC0U) == 0x80U &&
        (bytes[2] & 0xC0U) == 0x80U) {
        codepoint = ((uint32_t)(bytes[0] & 0x0FU) << 12) |
                    ((uint32_t)(bytes[1] & 0x3FU) << 6) | (bytes[2] & 0x3FU);
        *cursor += 3;
        if(codepoint < 0x800U || (codepoint >= 0xD800U && codepoint <= 0xDFFFU)) {
            codepoint = 0xFFFDU;
        }
    } else if(
        (bytes[0] & 0xF8U) == 0xF0U && (bytes[1] & 0xC0U) == 0x80U &&
        (bytes[2] & 0xC0U) == 0x80U && (bytes[3] & 0xC0U) == 0x80U) {
        codepoint = ((uint32_t)(bytes[0] & 0x07U) << 18) |
                    ((uint32_t)(bytes[1] & 0x3FU) << 12) |
                    ((uint32_t)(bytes[2] & 0x3FU) << 6) | (bytes[3] & 0x3FU);
        *cursor += 4;
        if(codepoint < 0x10000U || codepoint > 0x10FFFFU) codepoint = 0xFFFDU;
    } else {
        codepoint = 0xFFFDU;
        *cursor += 1;
    }

    return codepoint;
}

static bool morse_player_append_segment(
    MorsePlayerSegment* segments,
    size_t segment_capacity,
    MorsePlayerProgramInfo* info,
    bool tone,
    uint8_t units) {
    if(units == 0U || units > MORSE_PLAYER_SEGMENT_UNITS_MASK ||
       info->segment_count >= segment_capacity) {
        return false;
    }

    segments[info->segment_count++] = (MorsePlayerSegment)(units | (tone ? MORSE_PLAYER_TONE_FLAG : 0U));
    info->total_units += units;
    return true;
}

bool morse_player_build_program(
    const char* text,
    MorsePlayerSegment* segments,
    size_t segment_capacity,
    MorsePlayerProgramInfo* info) {
    if(!text || !segments || !info || segment_capacity == 0U) return false;

    memset(info, 0, sizeof(*info));
    const char* cursor = text;
    bool has_character = false;
    bool pending_word_gap = false;

    while(*cursor != '\0') {
        uint32_t codepoint = morse_player_utf8_next(&cursor);
        if(codepoint == 0U) break;

        if(codepoint == ' ' || codepoint == '\t' || codepoint == '\r' || codepoint == '\n') {
            if(has_character) pending_word_gap = true;
            continue;
        }

        const char* pattern = morse_player_pattern_for_codepoint(codepoint);
        if(!pattern) {
            info->unknown_count++;
            continue;
        }

        if(has_character &&
           !morse_player_append_segment(
               segments, segment_capacity, info, false, pending_word_gap ? 7U : 3U)) {
            return false;
        }

        for(const char* symbol = pattern; *symbol != '\0'; symbol++) {
            if(!morse_player_append_segment(
                   segments, segment_capacity, info, true, *symbol == '.' ? 1U : 3U)) {
                return false;
            }

            if(symbol[1] != '\0' &&
               !morse_player_append_segment(segments, segment_capacity, info, false, 1U)) {
                return false;
            }
        }

        has_character = true;
        pending_word_gap = false;
    }

    return info->segment_count > 0U;
}

static bool morse_player_display_append(char* output, size_t capacity, size_t* length, const char* text) {
    const size_t text_length = strlen(text);
    if(*length + text_length + 1U > capacity) return false;
    memcpy(output + *length, text, text_length);
    *length += text_length;
    output[*length] = '\0';
    return true;
}

bool morse_player_build_display(
    const char* text,
    char* output,
    size_t output_capacity,
    size_t* unknown_count) {
    if(!text || !output || output_capacity == 0U) return false;

    output[0] = '\0';
    size_t length = 0U;
    size_t unknown = 0U;
    bool has_character = false;
    bool pending_word_gap = false;
    const char* cursor = text;

    while(*cursor != '\0') {
        uint32_t codepoint = morse_player_utf8_next(&cursor);
        if(codepoint == 0U) break;

        if(codepoint == ' ' || codepoint == '\t' || codepoint == '\r' || codepoint == '\n') {
            if(has_character) pending_word_gap = true;
            continue;
        }

        const char* pattern = morse_player_pattern_for_codepoint(codepoint);
        if(!pattern) {
            unknown++;
            continue;
        }

        if(has_character) {
            if(!morse_player_display_append(output, output_capacity, &length, pending_word_gap ? " / " : " ")) {
                return false;
            }
        }
        if(!morse_player_display_append(output, output_capacity, &length, pattern)) return false;

        has_character = true;
        pending_word_gap = false;
    }

    if(unknown_count) *unknown_count = unknown;
    return has_character;
}
