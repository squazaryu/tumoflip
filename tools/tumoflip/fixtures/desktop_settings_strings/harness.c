/* Exercise the production loader and private historical layouts without copying them. */
#include "applications/services/desktop/desktop_settings.c"

#include <stdio.h>

struct Storage {
    unsigned unused;
};

struct File {
    size_t position;
};

static Storage storage;
static uint8_t file_data[4096];
static size_t file_size;
static bool file_exists;
static bool fail_save;
static bool short_write;
static unsigned save_attempts;

#define REQUIRE(condition, message)                     \
    do {                                                \
        if(!(condition)) {                              \
            fprintf(stderr, "FAIL: %s\n", (message));  \
            exit(1);                                    \
        }                                               \
    } while(false)

void* furi_record_open(const char* name) {
    REQUIRE(strcmp(name, RECORD_STORAGE) == 0, "unexpected record");
    return &storage;
}

void furi_record_close(const char* name) {
    REQUIRE(strcmp(name, RECORD_STORAGE) == 0, "unexpected record close");
}

File* storage_file_alloc(Storage* instance) {
    REQUIRE(instance == &storage, "unexpected storage");
    return calloc(1, sizeof(File));
}

bool storage_file_open(File* file, const char* path, FS_AccessMode access, FS_OpenMode mode) {
    REQUIRE(strcmp(path, DESKTOP_SETTINGS_PATH) == 0, "settings path changed");
    file->position = 0;
    if(access == FSAM_READ) {
        REQUIRE(mode == FSOM_OPEN_EXISTING, "unexpected read mode");
        return file_exists;
    }
    REQUIRE(mode == FSOM_CREATE_ALWAYS, "unexpected write mode");
    save_attempts++;
    if(fail_save) return false;
    file_exists = true;
    file_size = 0;
    return true;
}

size_t storage_file_read(File* file, void* data, size_t size) {
    size_t available = file_size - file->position;
    if(size > available) size = available;
    memcpy(data, file_data + file->position, size);
    file->position += size;
    return size;
}

size_t storage_file_write(File* file, const void* data, size_t size) {
    if(short_write) return 0;
    REQUIRE(file->position + size <= sizeof(file_data), "fake file overflow");
    memcpy(file_data + file->position, data, size);
    file->position += size;
    file_size = file->position;
    return size;
}

uint64_t storage_file_size(File* file) {
    (void)file;
    return file_size;
}

const char* storage_file_get_error_desc(File* file) {
    (void)file;
    return "injected storage failure";
}

void storage_file_close(File* file) {
    (void)file;
}

void storage_file_free(File* file) {
    free(file);
}

static void fill_apps(FavoriteApp* apps, bool malformed) {
    for(size_t i = 0; i < FavoriteAppNumber; i++) {
        memset(apps[i].name_or_path, 'A' + i, sizeof(apps[i].name_or_path));
        if(!malformed) {
            /* Empty, one-character, short, embedded NUL, and maximum-length names. */
            const size_t lengths[] = {0, 1, 20, 63, 127};
            apps[i].name_or_path[lengths[i]] = '\0';
            apps[i].name_or_path[sizeof(apps[i].name_or_path) - 1] = '\0';
        }
    }
}

static void write_fixture(const void* data, size_t size, uint8_t version) {
    REQUIRE(
        saved_struct_save(DESKTOP_SETTINGS_PATH, data, size, DESKTOP_SETTINGS_MAGIC, version),
        "fixture write failed");
    void* loaded = malloc(size);
    REQUIRE(
        saved_struct_load(DESKTOP_SETTINGS_PATH, loaded, size, DESKTOP_SETTINGS_MAGIC, version),
        "fixture must pass real saved_struct checksum, magic, version and size checks");
    REQUIRE(memcmp(loaded, data, size) == 0, "saved_struct fixture round trip");
    free(loaded);
    save_attempts = 0;
}

static DesktopSettings prepare_fixture(uint8_t version, bool malformed) {
    DesktopSettings expected = {0};
    expected.auto_lock_delay_ms = 123456789;
    expected.usb_inhibit_auto_lock = version == 14 ? 0 : 1;
    expected.displayBatteryPercentage = DISPLAY_BATTERY_BAR_PERCENT;
    expected.display_clock = 1;
    expected.lockscreen_skip_animation = version >= 19 ? 1 : 0;
    fill_apps(expected.favorite_apps, malformed);

    /* Distinct retired-field values detect accidental mapping into current fields. */
    if(version == 14) {
        DesktopSettingsV14 old = {0};
        old.auto_lock_delay_ms = expected.auto_lock_delay_ms;
        old.displayBatteryPercentage = expected.displayBatteryPercentage;
        old.dummy_mode = 0xB1;
        old.display_clock = expected.display_clock;
        memcpy(old.favorite_apps, expected.favorite_apps, sizeof(old.favorite_apps));
        memset(old.dummy_apps, 0xE1, sizeof(old.dummy_apps));
        write_fixture(&old, sizeof(old), version);
    } else if(version == 17) {
        DesktopSettingsV17 old = {0};
        old.auto_lock_delay_ms = expected.auto_lock_delay_ms;
        old.usb_inhibit_auto_lock = expected.usb_inhibit_auto_lock;
        old.displayBatteryPercentage = expected.displayBatteryPercentage;
        old.dummy_mode = 0xB2;
        old.display_clock = expected.display_clock;
        memcpy(old.favorite_apps, expected.favorite_apps, sizeof(old.favorite_apps));
        memset(old.dummy_apps, 0xE2, sizeof(old.dummy_apps));
        write_fixture(&old, sizeof(old), version);
    } else if(version == 18) {
        DesktopSettingsV18 old = {0};
        old.auto_lock_delay_ms = expected.auto_lock_delay_ms;
        old.usb_inhibit_auto_lock = expected.usb_inhibit_auto_lock;
        old.displayBatteryPercentage = expected.displayBatteryPercentage;
        old.display_clock = expected.display_clock;
        memcpy(old.favorite_apps, expected.favorite_apps, sizeof(old.favorite_apps));
        write_fixture(&old, sizeof(old), version);
    } else if(version == 19) {
        DesktopSettingsV19 old = {0};
        old.auto_lock_delay_ms = expected.auto_lock_delay_ms;
        old.usb_inhibit_auto_lock = expected.usb_inhibit_auto_lock;
        old.displayBatteryPercentage = expected.displayBatteryPercentage;
        old.display_clock = expected.display_clock;
        old.lockscreen_skip_animation = expected.lockscreen_skip_animation;
        memcpy(old.favorite_apps, expected.favorite_apps, sizeof(old.favorite_apps));
        write_fixture(&old, sizeof(old), version);
    } else if(version == 20) {
        DesktopSettingsV20 old = {0};
        old.auto_lock_delay_ms = expected.auto_lock_delay_ms;
        old.usb_inhibit_auto_lock = expected.usb_inhibit_auto_lock;
        old.displayBatteryPercentage = expected.displayBatteryPercentage;
        old.display_clock = expected.display_clock;
        old.lockscreen_skip_animation = expected.lockscreen_skip_animation;
        old.fap_loading_animation = 0xB3;
        memcpy(old.favorite_apps, expected.favorite_apps, sizeof(old.favorite_apps));
        write_fixture(&old, sizeof(old), version);
    } else {
        REQUIRE(version == DESKTOP_SETTINGS_VER, "unknown fixture version");
        write_fixture(&expected, sizeof(expected), version);
    }
    return expected;
}

static void check_settings(const DesktopSettings* actual, const DesktopSettings* expected) {
    REQUIRE(actual->auto_lock_delay_ms == expected->auto_lock_delay_ms, "auto lock changed");
    REQUIRE(
        actual->usb_inhibit_auto_lock == expected->usb_inhibit_auto_lock, "USB inhibit changed");
    REQUIRE(
        actual->displayBatteryPercentage == expected->displayBatteryPercentage,
        "battery display changed");
    REQUIRE(actual->display_clock == expected->display_clock, "clock changed");
    REQUIRE(
        actual->lockscreen_skip_animation == expected->lockscreen_skip_animation,
        "lockscreen animation changed");
    for(size_t i = 0; i < FavoriteAppNumber; i++) {
        const char* name = actual->favorite_apps[i].name_or_path;
        REQUIRE(name[127] == '\0', "loaded shortcut is not NUL-terminated at its boundary");
        REQUIRE(memcmp(name, expected->favorite_apps[i].name_or_path, 127) == 0,
                "shortcut prefix or shortcut order changed");
    }
}

static void check_saved(const DesktopSettings* expected) {
    uint8_t magic;
    uint8_t version;
    size_t payload_size;
    REQUIRE(saved_struct_get_metadata(DESKTOP_SETTINGS_PATH, &magic, &version, &payload_size),
            "saved metadata missing");
    REQUIRE(magic == 0x17 && version == 21, "saved schema identity changed");
    REQUIRE(payload_size == 648, "saved payload size changed");
    DesktopSettings saved;
    REQUIRE(saved_struct_load(DESKTOP_SETTINGS_PATH, &saved, sizeof(saved), magic, version),
            "saved migration fails checksum/header validation");
    for(size_t i = 0; i < FavoriteAppNumber; i++) {
        REQUIRE(saved.favorite_apps[i].name_or_path[127] == '\0',
                "migration saved a shortcut before NUL sanitation");
    }
    check_settings(&saved, expected);
}

static void check_layout(void) {
    REQUIRE(FavoriteAppNumber == 5, "shortcut count changed");
    REQUIRE(sizeof(FavoriteApp) == 128, "shortcut width changed");
    REQUIRE(sizeof(DesktopSettings) == 648, "current on-disk size changed");
    REQUIRE(offsetof(DesktopSettings, favorite_apps) == 8, "shortcut offset changed");
    REQUIRE(sizeof(DesktopSettingsV14) == 1800, "v14 size changed");
    REQUIRE(sizeof(DesktopSettingsV17) == 1800, "v17 size changed");
    REQUIRE(sizeof(DesktopSettingsV18) == 648, "v18 size changed");
    REQUIRE(sizeof(DesktopSettingsV19) == 648, "v19 size changed");
    REQUIRE(sizeof(DesktopSettingsV20) == 652, "v20 size changed");
}

int main(int argc, char** argv) {
    REQUIRE(argc == 3, "usage: harness CASE VERSION");
    check_layout();
    const char* mode = argv[1];
    uint8_t version = (uint8_t)atoi(argv[2]);
    bool valid = strcmp(mode, "valid") == 0;
    DesktopSettings expected = prepare_fixture(version, !valid);
    DesktopSettings loaded;
    memset(&loaded, 0xCD, sizeof(loaded));

    bool defaults = false;
    if(strcmp(mode, "missing") == 0) {
        file_exists = false;
        defaults = true;
    } else if(strcmp(mode, "short-header") == 0) {
        file_size = 3;
        defaults = true;
    } else if(strcmp(mode, "short-payload") == 0) {
        file_size--;
        defaults = true;
    } else if(strcmp(mode, "bad-magic") == 0) {
        file_data[0] ^= 0xFF;
        defaults = true;
    } else if(strcmp(mode, "bad-checksum") == 0) {
        file_data[2] ^= 0xFF;
        defaults = true;
    } else if(strcmp(mode, "unsupported-version") == 0) {
        file_data[1] = 15;
        defaults = true;
    } else if(strcmp(mode, "save-failure") == 0) {
        fail_save = true;
    } else if(strcmp(mode, "short-write") == 0) {
        short_write = true;
    } else if(strcmp(mode, "defaults-save-failure") == 0) {
        file_exists = false;
        fail_save = true;
        defaults = true;
    } else {
        REQUIRE(valid || strcmp(mode, "malformed") == 0 || strcmp(mode, "migration-saved") == 0,
                "unknown test case");
    }

    desktop_settings_load(&loaded);
    if(defaults) {
        memset(&expected, 0, sizeof(expected));
        REQUIRE(memcmp(&loaded, &expected, sizeof(loaded)) == 0, "failed load did not reset defaults");
        REQUIRE(save_attempts == 1, "defaults should be saved once");
        if(!fail_save) check_saved(&expected);
    } else {
        REQUIRE(save_attempts == (version == 21 ? 0U : 1U), "unexpected settings rewrite count");
        /* Check disk first to catch a fix applied only after migration saving. */
        if(strcmp(mode, "migration-saved") == 0) check_saved(&expected);
        check_settings(&loaded, &expected);
        if(valid) REQUIRE(memcmp(&loaded, &expected, sizeof(loaded)) == 0, "valid settings changed");
        if(version != 21 && !fail_save && !short_write) check_saved(&expected);
    }
    printf("PASS: %s v%u\n", mode, version);
    return 0;
}
