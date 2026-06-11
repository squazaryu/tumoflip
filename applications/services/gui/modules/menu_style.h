#pragma once

#include <stdint.h>

#ifdef MENU_STYLE_STATIC
#include <saved_struct.h>
#include <storage/storage.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MenuStyleList,
    MenuStyleWii,
    MenuStyleDsi,
    MenuStyleVertical,
    MenuStyleWiiVertical,
    MenuStyleCount,
} MenuStyle;

#ifdef MENU_STYLE_STATIC

#define MENU_STYLE_SETTINGS_PATH  INT_PATH(".menustyles.settings")
#define MENU_STYLE_SETTINGS_MAGIC (0x87)
#define MENU_STYLE_SETTINGS_VER   (3)
#define MENU_STYLE_SETTINGS_VER_2 (2)

typedef struct {
    uint8_t style;
} MenuStyleSettings;

static inline const char* menu_style_get_name(MenuStyle style) {
    static const char* const names[MenuStyleCount] = {
        "List",
        "Wii",
        "DSi",
        "Vertical",
        "Wii Vertical",
    };

    return style < MenuStyleCount ? names[style] : names[MenuStyleList];
}

static inline MenuStyle menu_style_load(void) {
    MenuStyleSettings settings = {.style = MenuStyleList};

    if(saved_struct_load(
           MENU_STYLE_SETTINGS_PATH,
           &settings,
           sizeof(settings),
           MENU_STYLE_SETTINGS_MAGIC,
           MENU_STYLE_SETTINGS_VER) &&
       settings.style < MenuStyleCount) {
        return (MenuStyle)settings.style;
    }

    if(saved_struct_load(
           MENU_STYLE_SETTINGS_PATH,
           &settings,
           sizeof(settings),
           MENU_STYLE_SETTINGS_MAGIC,
           MENU_STYLE_SETTINGS_VER_2) &&
       settings.style < MenuStyleCount) {
        saved_struct_save(
            MENU_STYLE_SETTINGS_PATH,
            &settings,
            sizeof(settings),
            MENU_STYLE_SETTINGS_MAGIC,
            MENU_STYLE_SETTINGS_VER);
        return (MenuStyle)settings.style;
    }

    return MenuStyleList;
}

static inline void menu_style_save(MenuStyle style) {
    if(style >= MenuStyleCount) {
        style = MenuStyleList;
    }

    const MenuStyleSettings settings = {.style = style};
    saved_struct_save(
        MENU_STYLE_SETTINGS_PATH,
        &settings,
        sizeof(settings),
        MENU_STYLE_SETTINGS_MAGIC,
        MENU_STYLE_SETTINGS_VER);
}

#else

const char* menu_style_get_name(MenuStyle style);
MenuStyle menu_style_load(void);
void menu_style_save(MenuStyle style);

#endif

#ifdef __cplusplus
}
#endif
