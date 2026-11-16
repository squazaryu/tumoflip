#pragma once
#include <furi.h>
#include <flipper_format/flipper_format.h>
#include <string.h>

// Source-private display adaptation. Stored Protocol/Key and decoder IDs do not
// change, and no additional resident SDK exports are required by ARF's history.
static inline void
    subghz_history_name_variant(FuriString* name, FlipperFormat* format, FuriString* scratch) {
    if(!name || !format || !scratch || !flipper_format_rewind(format)) return;
    const char* protocol = furi_string_get_cstr(name);
    if(strcmp(protocol, "Faac SLH") == 0) {
        if(flipper_format_read_string(format, "Manufacture", scratch)) {
            const char* manufacturer = furi_string_get_cstr(scratch);
            if(*manufacturer && strcmp(manufacturer, "FAAC_SLH") != 0) {
                furi_string_set(name, scratch);
            }
        }
        return;
    }
    if(strcmp(protocol, "CAME") != 0 && strcmp(protocol, "Nice FloR-S") != 0) return;
    uint32_t bits = 0;
    if(!flipper_format_read_uint32(format, "Bit", &bits, 1)) return;
    if(strcmp(protocol, "CAME") == 0) {
        if(bits == 25)
            furi_string_set_str(name, "Prastel");
        else if(bits == 18)
            furi_string_set_str(name, "Airforce");
        // Prastel42 is a separate dynamic protocol in Tumoflip, not CAME.
        return;
    }
    uint8_t ic[2];
    uint32_t unknown = 0;
    bool o_code = flipper_format_rewind(format) &&
                  flipper_format_read_hex(format, "IC", ic, sizeof(ic));
    if(!o_code && flipper_format_rewind(format) &&
       flipper_format_read_uint32(format, "No_IC", &unknown, 1) && unknown <= 1) {
        o_code = true;
    }
    if(o_code)
        furi_string_set_str(name, "Nice O-Code");
    else if(bits == 72)
        furi_string_set_str(name, "Nice One");
}
