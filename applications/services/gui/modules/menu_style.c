#include "menu_style.h"

#include <saved_struct.h>
#include <storage/storage.h>

#define MENU_STYLE_SETTINGS_PATH  INT_PATH(".menustyles.settings")
#define MENU_STYLE_SETTINGS_MAGIC (0x87)
#define MENU_STYLE_SETTINGS_VER   (3)
#define MENU_STYLE_SETTINGS_VER_2 (2)

typedef struct {
    uint8_t style;
} MenuStyleSettings;

const char* menu_style_get_name(MenuStyle style) {
    static const char* const names[MenuStyleCount] = {
        "List",
        "Matrix",
        "Rail",
        "Side List",
        "Side Grid",
    };

    return style < MenuStyleCount ? names[style] : names[MenuStyleList];
}

MenuStyle menu_style_load(void) {
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

void menu_style_save(MenuStyle style) {
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
