#include "desktop_settings.h"
#include "desktop_settings_filename.h"

#include <saved_struct.h>
#include <storage/storage.h>

#define TAG "DesktopSettings"

#define DESKTOP_SETTINGS_VER_14 (14)
#define DESKTOP_SETTINGS_VER_17 (17)
#define DESKTOP_SETTINGS_VER_18 (18)
#define DESKTOP_SETTINGS_VER_19 (19)
#define DESKTOP_SETTINGS_VER_20 (20)
#define DESKTOP_SETTINGS_VER    (21)

#define DESKTOP_SETTINGS_PATH  INT_PATH(DESKTOP_SETTINGS_FILE_NAME)
#define DESKTOP_SETTINGS_MAGIC (0x17)

typedef struct {
    uint32_t auto_lock_delay_ms;
    uint8_t displayBatteryPercentage;
    uint8_t dummy_mode;
    uint8_t display_clock;
    FavoriteApp favorite_apps[FavoriteAppNumber];
    FavoriteApp dummy_apps[9];
} DesktopSettingsV14;

typedef struct {
    uint32_t auto_lock_delay_ms;
    uint8_t usb_inhibit_auto_lock;
    uint8_t displayBatteryPercentage;
    uint8_t dummy_mode;
    uint8_t display_clock;
    FavoriteApp favorite_apps[FavoriteAppNumber];
    FavoriteApp dummy_apps[9];
} DesktopSettingsV17;

typedef struct {
    uint32_t auto_lock_delay_ms;
    uint8_t usb_inhibit_auto_lock;
    uint8_t displayBatteryPercentage;
    uint8_t display_clock;
    FavoriteApp favorite_apps[FavoriteAppNumber];
} DesktopSettingsV18;

typedef struct {
    uint32_t auto_lock_delay_ms;
    uint8_t usb_inhibit_auto_lock;
    uint8_t displayBatteryPercentage;
    uint8_t display_clock;
    uint8_t lockscreen_skip_animation;
    FavoriteApp favorite_apps[FavoriteAppNumber];
} DesktopSettingsV19;

typedef struct {
    uint32_t auto_lock_delay_ms;
    uint8_t usb_inhibit_auto_lock;
    uint8_t displayBatteryPercentage;
    uint8_t display_clock;
    uint8_t lockscreen_skip_animation;
    uint8_t fap_loading_animation;
    FavoriteApp favorite_apps[FavoriteAppNumber];
} DesktopSettingsV20;

static void desktop_settings_migrate_from_v14(
    DesktopSettings* settings,
    const DesktopSettingsV14* settings_v14) {
    settings->auto_lock_delay_ms = settings_v14->auto_lock_delay_ms;
    settings->usb_inhibit_auto_lock = 0;
    settings->displayBatteryPercentage = settings_v14->displayBatteryPercentage;
    settings->display_clock = settings_v14->display_clock;
    settings->lockscreen_skip_animation = 0;
    memcpy(settings->favorite_apps, settings_v14->favorite_apps, sizeof(settings->favorite_apps));
}

static void desktop_settings_migrate_from_v17(
    DesktopSettings* settings,
    const DesktopSettingsV17* settings_v17) {
    settings->auto_lock_delay_ms = settings_v17->auto_lock_delay_ms;
    settings->usb_inhibit_auto_lock = settings_v17->usb_inhibit_auto_lock;
    settings->displayBatteryPercentage = settings_v17->displayBatteryPercentage;
    settings->display_clock = settings_v17->display_clock;
    settings->lockscreen_skip_animation = 0;
    memcpy(settings->favorite_apps, settings_v17->favorite_apps, sizeof(settings->favorite_apps));
}

static void desktop_settings_migrate_from_v18(
    DesktopSettings* settings,
    const DesktopSettingsV18* settings_v18) {
    settings->auto_lock_delay_ms = settings_v18->auto_lock_delay_ms;
    settings->usb_inhibit_auto_lock = settings_v18->usb_inhibit_auto_lock;
    settings->displayBatteryPercentage = settings_v18->displayBatteryPercentage;
    settings->display_clock = settings_v18->display_clock;
    settings->lockscreen_skip_animation = 0;
    memcpy(settings->favorite_apps, settings_v18->favorite_apps, sizeof(settings->favorite_apps));
}

static void desktop_settings_migrate_from_v19(
    DesktopSettings* settings,
    const DesktopSettingsV19* settings_v19) {
    settings->auto_lock_delay_ms = settings_v19->auto_lock_delay_ms;
    settings->usb_inhibit_auto_lock = settings_v19->usb_inhibit_auto_lock;
    settings->displayBatteryPercentage = settings_v19->displayBatteryPercentage;
    settings->display_clock = settings_v19->display_clock;
    settings->lockscreen_skip_animation = settings_v19->lockscreen_skip_animation;
    memcpy(settings->favorite_apps, settings_v19->favorite_apps, sizeof(settings->favorite_apps));
}

static void desktop_settings_migrate_from_v20(
    DesktopSettings* settings,
    const DesktopSettingsV20* settings_v20) {
    settings->auto_lock_delay_ms = settings_v20->auto_lock_delay_ms;
    settings->usb_inhibit_auto_lock = settings_v20->usb_inhibit_auto_lock;
    settings->displayBatteryPercentage = settings_v20->displayBatteryPercentage;
    settings->display_clock = settings_v20->display_clock;
    settings->lockscreen_skip_animation = settings_v20->lockscreen_skip_animation;
    memcpy(settings->favorite_apps, settings_v20->favorite_apps, sizeof(settings->favorite_apps));
}

void desktop_settings_load(DesktopSettings* settings) {
    furi_assert(settings);

    bool success = false;

    do {
        uint8_t version;
        if(!saved_struct_get_metadata(DESKTOP_SETTINGS_PATH, NULL, &version, NULL)) break;

        if(version == DESKTOP_SETTINGS_VER) {
            success = saved_struct_load(
                DESKTOP_SETTINGS_PATH,
                settings,
                sizeof(DesktopSettings),
                DESKTOP_SETTINGS_MAGIC,
                DESKTOP_SETTINGS_VER);

        } else if(version == DESKTOP_SETTINGS_VER_20) {
            DesktopSettingsV20* settings_v20 = malloc(sizeof(DesktopSettingsV20));

            success = saved_struct_load(
                DESKTOP_SETTINGS_PATH,
                settings_v20,
                sizeof(DesktopSettingsV20),
                DESKTOP_SETTINGS_MAGIC,
                DESKTOP_SETTINGS_VER_20);

            if(success) {
                desktop_settings_migrate_from_v20(settings, settings_v20);
                desktop_settings_save(settings);
            }

            free(settings_v20);

        } else if(version == DESKTOP_SETTINGS_VER_19) {
            DesktopSettingsV19* settings_v19 = malloc(sizeof(DesktopSettingsV19));

            success = saved_struct_load(
                DESKTOP_SETTINGS_PATH,
                settings_v19,
                sizeof(DesktopSettingsV19),
                DESKTOP_SETTINGS_MAGIC,
                DESKTOP_SETTINGS_VER_19);

            if(success) {
                desktop_settings_migrate_from_v19(settings, settings_v19);
                desktop_settings_save(settings);
            }

            free(settings_v19);

        } else if(version == DESKTOP_SETTINGS_VER_18) {
            DesktopSettingsV18* settings_v18 = malloc(sizeof(DesktopSettingsV18));

            success = saved_struct_load(
                DESKTOP_SETTINGS_PATH,
                settings_v18,
                sizeof(DesktopSettingsV18),
                DESKTOP_SETTINGS_MAGIC,
                DESKTOP_SETTINGS_VER_18);

            if(success) {
                desktop_settings_migrate_from_v18(settings, settings_v18);
                desktop_settings_save(settings);
            }

            free(settings_v18);

        } else if(version == DESKTOP_SETTINGS_VER_17) {
            DesktopSettingsV17* settings_v17 = malloc(sizeof(DesktopSettingsV17));

            success = saved_struct_load(
                DESKTOP_SETTINGS_PATH,
                settings_v17,
                sizeof(DesktopSettingsV17),
                DESKTOP_SETTINGS_MAGIC,
                DESKTOP_SETTINGS_VER_17);

            if(success) {
                desktop_settings_migrate_from_v17(settings, settings_v17);
                desktop_settings_save(settings);
            }

            free(settings_v17);

        } else if(version == DESKTOP_SETTINGS_VER_14) {
            DesktopSettingsV14* settings_v14 = malloc(sizeof(DesktopSettingsV14));

            success = saved_struct_load(
                DESKTOP_SETTINGS_PATH,
                settings_v14,
                sizeof(DesktopSettingsV14),
                DESKTOP_SETTINGS_MAGIC,
                DESKTOP_SETTINGS_VER_14);

            if(success) {
                desktop_settings_migrate_from_v14(settings, settings_v14);
                desktop_settings_save(settings);
            }

            free(settings_v14);
        }

    } while(false);

    if(!success) {
        FURI_LOG_W(TAG, "Failed to load file, using defaults");
        memset(settings, 0, sizeof(DesktopSettings));
        desktop_settings_save(settings);
    }
}

void desktop_settings_save(const DesktopSettings* settings) {
    furi_assert(settings);

    const bool success = saved_struct_save(
        DESKTOP_SETTINGS_PATH,
        settings,
        sizeof(DesktopSettings),
        DESKTOP_SETTINGS_MAGIC,
        DESKTOP_SETTINGS_VER);

    if(!success) {
        FURI_LOG_E(TAG, "Failed to save file");
    }
}
