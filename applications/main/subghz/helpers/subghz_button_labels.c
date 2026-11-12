#include "subghz_button_labels.h"

#include <furi.h>
#include <lib/subghz/blocks/custom_btn.h>
#include <string.h>

static const char* const button_default_labels[SUBGHZ_BUTTON_LABEL_COUNT] = {
    "Original",
    "Up",
    "Down",
    "Left",
    "Right",
    "Button 5",
    "Button 6",
    "Button 7",
};

// Store one-byte label IDs instead of eight flash pointers per protocol.
enum {
    LabelDefault,
    LabelOriginal,
    LabelLock,
    LabelUnlock,
    LabelTrunk,
    LabelPanic,
    LabelOpen,
    LabelRemoteStart,
    LabelHorn,
    LabelExtra,
    LabelStart,
    LabelBtn1,
    LabelBtn2,
    LabelBtn3,
    LabelBtn4,
};

static const char* const protocol_label_text[] = {
    [LabelDefault] = NULL,
    [LabelOriginal] = "Original",
    [LabelLock] = "Lock",
    [LabelUnlock] = "Unlock",
    [LabelTrunk] = "Trunk",
    [LabelPanic] = "Panic",
    [LabelOpen] = "Open",
    [LabelRemoteStart] = "Remote Start",
    [LabelHorn] = "Horn",
    [LabelExtra] = "Extra",
    [LabelStart] = "Start",
    [LabelBtn1] = "Btn 1",
    [LabelBtn2] = "Btn 2",
    [LabelBtn3] = "Btn 3",
    [LabelBtn4] = "Btn 4",
};

typedef struct {
    const char* protocol;
    uint8_t max_custom_btn;
    uint8_t labels[SUBGHZ_BUTTON_LABEL_COUNT];
} SubGhzProtocolButtonLabels;

static const SubGhzProtocolButtonLabels protocol_button_labels[] = {
    {"VAG GROUP", 4, {LabelOriginal, LabelLock, LabelUnlock, LabelTrunk, LabelPanic}},
    {"Porsche AG", 4, {LabelOriginal, LabelLock, LabelUnlock, LabelTrunk, LabelOpen}},
    {"FORD V0", 3, {LabelOriginal, LabelLock, LabelUnlock, LabelTrunk}},
    {"Ford V2", 4, {LabelUnlock, LabelLock, LabelTrunk, LabelPanic, LabelRemoteStart}},
    {"PSA GROUP", 4, {LabelOriginal, LabelLock, LabelUnlock, LabelTrunk, LabelTrunk}},
    {"PSA OLD", 4, {LabelOriginal, LabelLock, LabelUnlock, LabelTrunk, LabelTrunk}},
    {"KIA/HYU V0", 4, {LabelOriginal, LabelLock, LabelUnlock, LabelTrunk, LabelHorn}},
    {"KIA/HYU V1", 4, {LabelOriginal, LabelLock, LabelUnlock, LabelTrunk, LabelPanic}},
    {"KIA/HYU V2", 4, {LabelOriginal, LabelLock, LabelUnlock, LabelTrunk, LabelPanic}},
    {"KIA/HYU V3", 5, {LabelOriginal, LabelLock, LabelUnlock, LabelTrunk, LabelPanic, LabelHorn}},
    {"KIA/HYU V4", 5, {LabelOriginal, LabelLock, LabelUnlock, LabelTrunk, LabelPanic, LabelHorn}},
    {"KIA/HYU V3/V4",
     5,
     {LabelOriginal, LabelLock, LabelUnlock, LabelTrunk, LabelPanic, LabelHorn}},
    {"KIA/HYU V5", 4, {LabelOriginal, LabelUnlock, LabelLock, LabelTrunk, LabelHorn}},
    {"KIA/HYU V6", 4, {LabelOriginal, LabelLock, LabelUnlock, LabelTrunk, LabelPanic}},
    {"SUBARU", 5, {LabelOriginal, LabelLock, LabelUnlock, LabelTrunk, LabelPanic, LabelExtra}},
    {"SUZUKI", 4, {LabelOriginal, LabelLock, LabelUnlock, LabelTrunk, LabelPanic}},
    {"Star Line", 4, {LabelOriginal, LabelLock, LabelUnlock, LabelTrunk, LabelStart}},
    {"Scher-Khan", 4, {LabelOriginal, LabelLock, LabelUnlock, LabelTrunk, LabelStart}},
    {"Sheriff CFM", 4, {LabelOriginal, LabelLock, LabelUnlock, LabelTrunk, LabelPanic}},
    {"Nice FloR-S", 4, {LabelOriginal, LabelBtn1, LabelBtn2, LabelBtn3, LabelBtn4}},
    {"CAME Atomo", 3, {LabelOriginal, LabelBtn1, LabelBtn2, LabelBtn3}},
    {"Alutech AT-4N", 4, {LabelOriginal, LabelBtn1, LabelBtn2, LabelBtn3, LabelBtn4}},
    {"KeeLoq", 4, {LabelOriginal, LabelBtn1, LabelBtn2, LabelBtn3, LabelBtn4}},
    {"Phoenix_V2", 4, {LabelOriginal, LabelBtn1, LabelBtn2, LabelBtn3, LabelBtn4}},
    {"Beninca ARC", 2, {LabelOriginal, LabelBtn1, LabelBtn2}},
    {"GangQi", 3, {LabelOriginal, LabelBtn1, LabelBtn2, LabelBtn3}},
    {"Hay21", 2, {LabelOriginal, LabelBtn1, LabelBtn2}},
    {"Hollarm", 3, {LabelOriginal, LabelBtn1, LabelBtn2, LabelBtn3}},
    {"Jarolift", 3, {LabelOriginal, LabelBtn1, LabelBtn2, LabelBtn3}},
    {"KingGates Stylo4k", 3, {LabelOriginal, LabelBtn1, LabelBtn2, LabelBtn3}},
    {"Princeton", 4, {LabelOriginal, LabelBtn1, LabelBtn2, LabelBtn3, LabelBtn4}},
    {"Roger", 3, {LabelOriginal, LabelBtn1, LabelBtn2, LabelBtn3}},
    {"Security+ 2.0", 4, {LabelOriginal, LabelBtn1, LabelBtn2, LabelBtn3, LabelBtn4}},
    {"Somfy Telis", 3, {LabelOriginal, LabelBtn1, LabelBtn2, LabelBtn3}},
    {"Faac SLH", 1, {LabelOriginal, LabelBtn1}},
};

void subghz_button_labels_reset(const char* labels[SUBGHZ_BUTTON_LABEL_COUNT]) {
    for(uint8_t i = 0; i < SUBGHZ_BUTTON_LABEL_COUNT; i++) {
        labels[i] = button_default_labels[i];
    }
}

void subghz_button_labels_apply_protocol(
    const char* protocol,
    const char* labels[SUBGHZ_BUTTON_LABEL_COUNT]) {
    if(!protocol) return;

    for(uint8_t i = 0; i < COUNT_OF(protocol_button_labels); i++) {
        if(strcmp(protocol, protocol_button_labels[i].protocol) == 0) {
            for(uint8_t btn = 0; btn < SUBGHZ_BUTTON_LABEL_COUNT; btn++) {
                if(protocol_button_labels[i].labels[btn]) {
                    labels[btn] = protocol_label_text[protocol_button_labels[i].labels[btn]];
                }
            }
            break;
        }
    }
}

const char* subghz_button_labels_get(
    const char* const labels[SUBGHZ_BUTTON_LABEL_COUNT],
    uint8_t custom_btn_id,
    uint8_t original_custom_btn) {
    if(custom_btn_id == SUBGHZ_CUSTOM_BTN_OK) {
        if((original_custom_btn != SUBGHZ_CUSTOM_BTN_OK) &&
           (original_custom_btn < SUBGHZ_BUTTON_LABEL_COUNT)) {
            return labels[original_custom_btn];
        }
    }

    if(custom_btn_id < SUBGHZ_BUTTON_LABEL_COUNT) {
        return labels[custom_btn_id];
    }

    return "Button";
}

uint8_t subghz_button_labels_get_max_custom_btn(const char* protocol) {
    if(!protocol) return 0;

    for(uint8_t i = 0; i < COUNT_OF(protocol_button_labels); i++) {
        if(strcmp(protocol, protocol_button_labels[i].protocol) == 0) {
            return protocol_button_labels[i].max_custom_btn;
        }
    }

    return 0;
}
