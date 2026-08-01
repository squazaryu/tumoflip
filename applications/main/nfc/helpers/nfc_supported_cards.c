#include "nfc_supported_cards.h"

#include "../plugins/supported_cards/nfc_supported_card_plugin.h"

#include <flipper_application/flipper_application.h>
#include <flipper_application/plugins/plugin_manager.h>
#include <loader/firmware_api/firmware_api.h>

#include <furi.h>
#include <path.h>

#define TAG "NfcSupportedCards"

#define NFC_SUPPORTED_CARDS_PLUGINS_PATH    APP_DATA_PATH("plugins")
#define NFC_SUPPORTED_CARDS_PLUGIN_SUFFIX   "_parser.fal"
#define NFC_SUPPORTED_CARDS_PLUGIN_NAME_MAX 64U

typedef enum {
    NfcSupportedCardsLoadStateIdle,
    NfcSupportedCardsLoadStateInProgress,
    NfcSupportedCardsLoadStateSuccess,
    NfcSupportedCardsLoadStateFail,
} NfcSupportedCardsLoadState;

typedef struct {
    Storage* storage;
    File* directory;
    char file_name[NFC_SUPPORTED_CARDS_PLUGIN_NAME_MAX];
    FlipperApplication* app;
} NfcSupportedCardsLoadContext;

struct NfcSupportedCards {
    CompositeApiResolver* api_resolver;
    NfcSupportedCardsLoadState load_state;
};

NfcSupportedCards* nfc_supported_cards_alloc(CompositeApiResolver* api_resolver) {
    NfcSupportedCards* instance = malloc(sizeof(NfcSupportedCards));
    instance->api_resolver = api_resolver;
    instance->load_state = NfcSupportedCardsLoadStateIdle;

    return instance;
}

void nfc_supported_cards_free(NfcSupportedCards* instance) {
    furi_assert(instance);
    free(instance);
}

static void nfc_supported_cards_load_context_init(NfcSupportedCardsLoadContext* instance) {
    instance->storage = furi_record_open(RECORD_STORAGE);
    instance->directory = storage_file_alloc(instance->storage);
    instance->app = NULL;

    if(!storage_dir_open(instance->directory, NFC_SUPPORTED_CARDS_PLUGINS_PATH)) {
        FURI_LOG_D(TAG, "Failed to open directory: %s", NFC_SUPPORTED_CARDS_PLUGINS_PATH);
    }
}

static void nfc_supported_cards_unload_plugin(NfcSupportedCardsLoadContext* instance) {
    if(instance->app) {
        flipper_application_free(instance->app);
        instance->app = NULL;
    }
}

static void nfc_supported_cards_load_context_deinit(NfcSupportedCardsLoadContext* instance) {
    nfc_supported_cards_unload_plugin(instance);

    storage_dir_close(instance->directory);
    storage_file_free(instance->directory);

    furi_record_close(RECORD_STORAGE);
}

static const NfcSupportedCardsPlugin* nfc_supported_cards_get_plugin(
    NfcSupportedCardsLoadContext* instance,
    const char* name,
    const ElfApiInterface* api_interface) {
    furi_assert(instance);
    furi_assert(name);

    const NfcSupportedCardsPlugin* plugin = NULL;
    char plugin_path
        [sizeof(NFC_SUPPORTED_CARDS_PLUGINS_PATH) + NFC_SUPPORTED_CARDS_PLUGIN_NAME_MAX +
         sizeof(NFC_SUPPORTED_CARDS_PLUGIN_SUFFIX)];
    const int path_length = snprintf(
        plugin_path,
        sizeof(plugin_path),
        "%s/%s%s",
        NFC_SUPPORTED_CARDS_PLUGINS_PATH,
        name,
        NFC_SUPPORTED_CARDS_PLUGIN_SUFFIX);
    if((path_length <= 0) || ((size_t)path_length >= sizeof(plugin_path))) return NULL;

    do {
        nfc_supported_cards_unload_plugin(instance);
        instance->app = flipper_application_alloc(instance->storage, api_interface);

        if(flipper_application_preload(instance->app, plugin_path) !=
           FlipperApplicationPreloadStatusSuccess)
            break;
        if(!flipper_application_is_plugin(instance->app)) break;
        if(flipper_application_map_to_memory(instance->app) != FlipperApplicationLoadStatusSuccess)
            break;
        const FlipperAppPluginDescriptor* descriptor =
            flipper_application_plugin_get_descriptor(instance->app);

        if(descriptor == NULL) break;

        if(strcmp(descriptor->appid, NFC_SUPPORTED_CARD_PLUGIN_APP_ID) != 0) break;
        if(descriptor->ep_api_version != NFC_SUPPORTED_CARD_PLUGIN_API_VERSION) break;

        plugin = descriptor->entry_point;
    } while(false);

    return plugin;
}

static const NfcSupportedCardsPlugin* nfc_supported_cards_get_next_plugin(
    NfcSupportedCardsLoadContext* instance,
    const ElfApiInterface* api_interface) {
    const NfcSupportedCardsPlugin* plugin = NULL;

    do {
        if(!storage_file_is_open(instance->directory)) break;
        if(!storage_dir_read(
               instance->directory, NULL, instance->file_name, sizeof(instance->file_name)))
            break;

        const size_t suffix_len = strlen(NFC_SUPPORTED_CARDS_PLUGIN_SUFFIX);
        const size_t file_name_len = strlen(instance->file_name);
        if(file_name_len <= suffix_len) break;

        size_t suffix_start_pos = file_name_len - suffix_len;
        if(memcmp(
               &instance->file_name[suffix_start_pos],
               NFC_SUPPORTED_CARDS_PLUGIN_SUFFIX,
               suffix_len) != 0) //-V1051
            break;

        // Trim suffix from file_name to save memory. The suffix will be concatenated on plugin load.
        instance->file_name[suffix_start_pos] = '\0';

        plugin = nfc_supported_cards_get_plugin(instance, instance->file_name, api_interface);
    } while(plugin == NULL); //-V654

    return plugin;
}

void nfc_supported_cards_load_cache(NfcSupportedCards* instance) {
    furi_assert(instance);

    if((instance->load_state == NfcSupportedCardsLoadStateSuccess) ||
       (instance->load_state == NfcSupportedCardsLoadStateFail)) {
        return;
    }

    instance->load_state = NfcSupportedCardsLoadStateInProgress;
    NfcSupportedCardsLoadContext load_context;
    nfc_supported_cards_load_context_init(&load_context);
    const bool plugins_available = storage_file_is_open(load_context.directory);
    nfc_supported_cards_load_context_deinit(&load_context);

    instance->load_state = plugins_available ? NfcSupportedCardsLoadStateSuccess :
                                               NfcSupportedCardsLoadStateFail;
    if(!plugins_available) FURI_LOG_D(TAG, "Plugins not found");
}

bool nfc_supported_cards_read(NfcSupportedCards* instance, NfcDevice* device, Nfc* nfc) {
    furi_assert(instance);
    furi_assert(device);
    furi_assert(nfc);

    bool card_read = false;
    NfcProtocol protocol = nfc_device_get_protocol(device);

    do {
        if(instance->load_state != NfcSupportedCardsLoadStateSuccess) break;

        NfcSupportedCardsLoadContext load_context;
        nfc_supported_cards_load_context_init(&load_context);
        while(true) {
            const ElfApiInterface* api_interface =
                composite_api_resolver_get(instance->api_resolver);
            const NfcSupportedCardsPlugin* plugin =
                nfc_supported_cards_get_next_plugin(&load_context, api_interface);
            if(plugin == NULL) break;
            if((plugin->protocol != protocol) || (plugin->read == NULL)) continue;

            if(plugin->verify) {
                if(!plugin->verify(nfc)) continue;
            }

            if(plugin->read(nfc, device)) {
                card_read = true;
                break;
            }
        }

        nfc_supported_cards_load_context_deinit(&load_context);
    } while(false);

    return card_read;
}

bool nfc_supported_cards_parse(
    NfcSupportedCards* instance,
    NfcDevice* device,
    FuriString* parsed_data) {
    furi_assert(instance);
    furi_assert(device);
    furi_assert(parsed_data);

    bool card_parsed = false;
    NfcProtocol protocol = nfc_device_get_protocol(device);

    do {
        if(instance->load_state != NfcSupportedCardsLoadStateSuccess) break;

        NfcSupportedCardsLoadContext load_context;
        nfc_supported_cards_load_context_init(&load_context);
        while(true) {
            const ElfApiInterface* api_interface =
                composite_api_resolver_get(instance->api_resolver);
            const NfcSupportedCardsPlugin* plugin =
                nfc_supported_cards_get_next_plugin(&load_context, api_interface);
            if(plugin == NULL) break;
            if((plugin->protocol != protocol) || (plugin->parse == NULL)) continue;

            if(plugin->parse(device, parsed_data)) {
                card_parsed = true;
                break;
            }
        }

        nfc_supported_cards_load_context_deinit(&load_context);
    } while(false);

    return card_parsed;
}
