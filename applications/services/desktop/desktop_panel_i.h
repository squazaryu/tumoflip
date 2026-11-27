#pragma once

/* Private extension: the public DesktopSettings v21 ABI is deliberately unchanged. */
#include "desktop_settings.h"
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* Presence denotes the feature, not a specific firmware tag or SDK minor. */
#define RECORD_DESKTOP_PANEL "tumoflip_desktop_panel_v2"

typedef enum {
    DesktopModeWallpaper,
    DesktopModeDashboard
} DesktopMode;

typedef struct {
    uint32_t generation;
    uint8_t mode;
    uint8_t motion;
    uint8_t reserved[2];
    FavoriteApp held[3];
} DesktopPanelSettings;

typedef struct {
    uint16_t heap_kb;
    uint16_t sleep_holds;
    uint16_t auto_lock_s;
    uint8_t sd_state; /* 0 unknown/error, 1 absent, 2 ready. */
    bool bt_on;
    bool fault_valid;
    uint32_t fault_commit;
    char fault_name[18];
} DesktopPanelSnapshot;

typedef struct {
    DesktopPanelSnapshot snapshot;
    uint32_t began;
    int16_t offset;
    int8_t direction;
    uint8_t page;
    bool active;
    bool motion;
} DesktopPanelModel;

static inline FavoriteApp*
    desktop_panel_held(DesktopPanelSettings* panel, FavoriteAppShortcut shortcut) {
    if(!panel) return NULL;
    switch(shortcut) {
    case FavoriteAppLeftLong:
        return &panel->held[0];
    case FavoriteAppRightLong:
        return &panel->held[1];
    case FavoriteAppOkLong:
        return &panel->held[2];
    default:
        return NULL;
    }
}

static inline void
    desktop_panel_defaults(DesktopPanelSettings* panel, const DesktopSettings* wallpaper) {
    memset(panel, 0, sizeof(*panel));
    panel->motion = 1;
    static const FavoriteAppShortcut held[] = {
        FavoriteAppLeftLong, FavoriteAppRightLong, FavoriteAppOkLong};
    for(size_t i = 0; i < 3; ++i) {
        panel->held[i] = wallpaper->favorite_apps[held[i]];
        panel->held[i].name_or_path[127] = '\0';
    }
}

static inline bool desktop_panel_valid(const DesktopPanelSettings* panel) {
    if(!panel || panel->mode > DesktopModeDashboard || panel->motion > 1 || panel->reserved[0] ||
       panel->reserved[1])
        return false;
    for(size_t i = 0; i < 3; ++i)
        if(!memchr(panel->held[i].name_or_path, '\0', sizeof(panel->held[i].name_or_path)))
            return false;
    return true;
}

static inline uint8_t desktop_panel_page(uint8_t page, int8_t direction) {
    if(page >= 3) return 0;
    if(direction > 0) return (page + 1) % 3;
    if(direction < 0) return (page + 2) % 3;
    return page;
}

/* One clipped incoming page; no screen-sized framebuffer allocation. */
static inline int16_t
    desktop_panel_offset(uint32_t began, uint32_t now, int8_t direction, bool awake) {
    uint32_t elapsed = now - began;
    if(!awake || !direction || elapsed >= 160) return 0;
    return (direction < 0 ? -1 : 1) * (int16_t)((160 - elapsed) * 112 / 160);
}

static inline uint32_t desktop_panel_period(bool awake, bool moving) {
    return awake && moving ? 40 : 0;
}

static inline uint8_t desktop_panel_icon_frame(uint32_t now, uint8_t count) {
    return count ? (now / 250) % count : 0;
}
