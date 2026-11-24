/* Isolated test doubles. No BLE transport, real storage, card, or key material. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define FURI_LOG_I(...)           ((void)0)
#define FURI_LOG_E(...)           ((void)0)
#define FURI_LOG_D(...)           ((void)0)
#define furi_check                assert
#define furi_assert               assert
#define FuriWaitForever           UINT32_MAX
#define HSEM                      0
#define CFG_HW_CLK48_CONFIG_SEMID 0
#define RECORD_STORAGE            "storage"
typedef int FuriMutex;
typedef int FuriHalBtStack;
typedef void (*BleGlueKeyStorageChangedCallback)(uint8_t*, uint16_t, void*);
typedef struct Profile Profile;
typedef struct {
    void (*stop)(Profile*);
} ProfileConfig;
struct Profile {
    const ProfileConfig* config;
};

/* Only the size matters; no pairing data or production keys are used. */
typedef struct {
    uint8_t bytes[32];
} GapRootSecurityKeys;
typedef const char FuriString;
typedef struct BtKeysStorage BtKeysStorage;
/*__KEY_STATE__*/

typedef struct {
    unsigned type;
    struct {
        struct {
            uint8_t* start_address;
            uint32_t size;
        } key_storage_data;
    } data;
} BtMessage;
typedef struct {
    BtMessage pending;
    bool has_message;
} MockQueue;
typedef struct {
    MockQueue* message_queue;
    BtKeysStorage* keys_storage;
    Profile* current_profile;
    int status;
} Bt;
typedef struct {
    bool available;
} Storage;
enum {
    FuriHalBusHSEM,
    FuriHalBusIPCC,
    FuriHalBusAES2,
    FuriHalBusPKA,
    FuriHalBusCRC,
    FuriMutexTypeNormal,
    FuriHalBtStackUnknown,
    FuriStatusOk,
    FSE_OK,
    FSE_NOT_READY,
    BtMessageTypeKeysStorageUpdated,
    BtStatusUnavailable
};
static Profile* current_profile;
static BleGlueKeyStorageChangedCallback listener;
static void* listener_context;
static bool glue_ready, event_on_restart, locked, insomniac, save_allowed = true;
static uint8_t fake_controller_ram[8];
static uint8_t fake_persisted[8];
static unsigned saved_length, saved_count, save_attempts, event_count, load_count, settings_count;
static unsigned serial_starts, bridge_binds, profile_stops;
static bool nvm_locked;
static Storage mock_storage = {.available = true};
static MockQueue mock_queue;
static void stop_profile(Profile* p) {
    assert(p == current_profile);
    profile_stops++;
}
static const ProfileConfig profile_config = {.stop = stop_profile};
static Profile profile = {.config = &profile_config};
static int dummy_template;
static void* ble_profile_serial = &dummy_template;

static void furi_hal_bt_reinit(void);
static void furi_hal_bt_set_key_storage_change_callback(BleGlueKeyStorageChangedCallback, void*);
static void bt_on_key_storage_change_callback(uint8_t*, uint16_t, void*);
static void bt_init_keys_settings(Bt*);
static void bt_start_application(Bt*);

void furi_hal_bus_enable(int bus) {
    (void)bus;
}
void furi_hal_bus_disable(int bus) {
    (void)bus;
}
FuriMutex* furi_mutex_alloc(int type) {
    assert(type == FuriMutexTypeNormal);
    static FuriMutex value;
    return &value;
}
int LL_HSEM_1StepLock(int h, int s) {
    (void)h;
    (void)s;
    return 0;
}
void ble_glue_init(void) {
    assert(!glue_ready);
    glue_ready = true;
    listener = NULL;
    listener_context = NULL;
}
void ble_glue_set_key_storage_changed_callback(BleGlueKeyStorageChangedCallback cb, void* ctx) {
    assert(glue_ready && cb && ctx);
    listener = cb;
    listener_context = ctx;
}
void furi_hal_bt_lock_core2(void) {
    assert(!locked);
    locked = true;
}
void furi_hal_bt_unlock_core2(void) {
    assert(locked);
    locked = false;
}
void furi_hal_power_insomnia_enter(void) {
    assert(!insomniac);
    insomniac = true;
}
void furi_hal_power_insomnia_exit(void) {
    assert(insomniac);
    insomniac = false;
}
void furi_hal_bt_stop_advertising(void) {
}
void hci_reset(void) {
}
void gap_thread_stop(void) {
}
void ble_app_deinit(void) {
}
bool ble_glue_reinit_c2(void) {
    assert(glue_ready);
    return true;
}
void ble_glue_stop(void) {
    assert(glue_ready);
    glue_ready = false;
    listener = NULL;
    listener_context = NULL;
}
void furi_delay_ms(uint32_t ms) {
    assert(ms == 100);
}
bool furi_hal_bt_start_radio_stack(void) {
    assert(glue_ready && !locked);
    if(event_on_restart && listener) {
        fake_controller_ram[0] = (uint8_t)(0x30 + event_count);
        listener(fake_controller_ram, 1, listener_context);
        event_count++;
    }
    return true;
}
int furi_message_queue_put(MockQueue* q, const BtMessage* msg, uint32_t wait) {
    assert(q && msg && wait == FuriWaitForever && !q->has_message);
    q->pending = *msg;
    q->has_message = true;
    return FuriStatusOk;
}
static const char* furi_string_get_cstr(FuriString* path) {
    return path;
}
static void furi_hal_bt_nvm_sram_sem_acquire(void) {
    assert(!nvm_locked);
    nvm_locked = true;
}
static void furi_hal_bt_nvm_sram_sem_release(void) {
    assert(nvm_locked);
    nvm_locked = false;
}
static bool saved_struct_save(
    const char* path,
    const void* data,
    size_t size,
    uint8_t magic,
    uint8_t version) {
    assert(!nvm_locked && !strcmp(path, "in-memory fixture"));
    assert(magic == BT_KEYS_STORAGE_MAGIC && version == BT_KEYS_STORAGE_VERSION);
    const BtKeysStorageFile* record = data;
    assert(size >= sizeof(record->root_keys) && record->root_keys.bytes[0] == 0xA5);
    const size_t length = size - sizeof(record->root_keys);
    assert(length <= sizeof(fake_persisted));
    save_attempts++;
    if(!save_allowed) return false;
    memcpy(fake_persisted, record->pairing_data, length);
    saved_length = (unsigned)length;
    saved_count++;
    return true;
}
void* furi_record_open(const char* name) {
    assert(!strcmp(name, RECORD_STORAGE));
    return &mock_storage;
}
void* storage_get_pubsub(Storage* storage) {
    assert(storage == &mock_storage);
    return storage;
}
void bt_storage_callback(void) {
}
void furi_pubsub_subscribe(void* pubsub, void (*callback)(void), Bt* bt) {
    assert(pubsub == &mock_storage && callback && bt);
}
int storage_sd_status(Storage* storage) {
    assert(storage == &mock_storage);
    return storage->available ? FSE_OK : FSE_NOT_READY;
}
void bt_load_keys(Bt* bt) {
    assert(bt);
    load_count++;
}
void bt_load_settings(Bt* bt) {
    assert(bt);
    settings_count++;
}
void* bt_keys_storage_get_root_keys(BtKeysStorage* keys) {
    assert(keys);
    return keys;
}
void bt_on_gap_event_callback(void) {
}
void bt_app_bridge_bind(Bt* bt) {
    assert(bt);
    bridge_binds++;
}
Profile*
    furi_hal_bt_change_app(void* type, void* args, void* keys, void (*event)(void), void* ctx) {
    assert(type == ble_profile_serial && !args && keys && event && ctx);
    serial_starts++;
    event_on_restart = true;
    furi_hal_bt_reinit();
    current_profile = &profile;
    return &profile;
}

/*__HAL_STATE__*/
/*__HAL_FUNCTIONS__*/
/*__SERVICE_CALLBACK__*/
/*__KEY_UPDATE__*/
/*__SERVICE_START_APP__*/
/*__SERVICE_RELOAD__*/
/*__SERVICE_INIT__*/

static void boot(Bt* bt) {
    /*__BOOT_BLOCK__*/
}

static void service_handle(Bt* bt) {
    assert(mock_queue.has_message);
    BtMessage message = mock_queue.pending;
    mock_queue.has_message = false;
    /*__SERVICE_UPDATE_BRANCH__*/
}

int main(void) {
    BtKeysStorage key_storage = {
        .nvm_sram_buff = fake_controller_ram,
        .nvm_sram_buff_size = sizeof(fake_controller_ram),
        .file_path = "in-memory fixture",
        .root_keys = {.bytes = {0xA5}},
    };
    Bt service = {.message_queue = &mock_queue, .keys_storage = &key_storage};
    furi_hal_bt_init();
    boot(&service);
    /* The radio restart within real service startup must keep the real callback. */
    assert(serial_starts == 1 && load_count == 1 && settings_count == 1 && bridge_binds == 1);
    if(!mock_queue.has_message) {
        fputs("callback lost during startup restart\n", stderr);
        return 1;
    }
    assert(mock_queue.pending.type == BtMessageTypeKeysStorageUpdated);
    assert(mock_queue.pending.data.key_storage_data.start_address == fake_controller_ram);
    service_handle(&service);
    assert(
        saved_count == 1 && save_attempts == 1 && saved_length == 1 && fake_persisted[0] == 0x30);

    /* A second restart exercises rebinding and queued persistence again. */
    furi_hal_bt_reinit();
    assert(mock_queue.has_message);
    service_handle(&service);
    assert(
        saved_count == 2 && fake_persisted[0] == 0x31 && profile_stops == 1 && !locked &&
        !insomniac);

    /* Synthetic save failure cannot be reported as success or alter old data. */
    save_allowed = false;
    fake_controller_ram[0] = 0x99;
    assert(!bt_keys_storage_update(&key_storage, fake_controller_ram, 1));
    assert(saved_count == 2 && save_attempts == 3 && fake_persisted[0] == 0x31);
    /* An out-of-range synthetic update cannot call save. */
    save_allowed = true;
    assert(!bt_keys_storage_update(&key_storage, fake_controller_ram + 7, 2));
    assert(saved_count == 2 && save_attempts == 3 && fake_persisted[0] == 0x31);

    /* New boot with unavailable SD: actual service takes no-load fallback. */
    furi_hal_bt = (FuriHalBt){.stack = FuriHalBtStackUnknown};
    glue_ready = false;
    listener = NULL;
    listener_context = NULL;
    current_profile = NULL;
    event_on_restart = false;
    mock_storage.available = false;
    save_allowed = false;
    service.current_profile = NULL;
    furi_hal_bt_init();
    boot(&service);
    assert(serial_starts == 2 && load_count == 1 && settings_count == 1 && bridge_binds == 2);
    assert(mock_queue.has_message);
    service_handle(&service);
    assert(
        saved_count == 2 && save_attempts == 4 && saved_length == 1 && fake_persisted[0] == 0x31);

    /* A valid nonzero offset extends the serialized prefix, not just the delta. */
    save_allowed = true;
    fake_controller_ram[4] = 0x5A;
    assert(bt_keys_storage_update(&key_storage, fake_controller_ram + 3, 2));
    assert(saved_count == 3 && save_attempts == 5 && saved_length == 5);
    assert(fake_persisted[0] == fake_controller_ram[0] && fake_persisted[4] == 0x5A);
    printf("PASS: callback → queue → synthetic persistence; 2 restarts, "
           "no-SD fallback and failure guards\n");
    return 0;
}
