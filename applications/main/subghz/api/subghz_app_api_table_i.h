#include "../subghz_i.h"
#include <lib/subghz/protocols/protocol_items.h>

// Private app exports, checked by the feature plugin ABI before entry.
static constexpr auto subghz_app_api_table = sort(create_array_t<sym_entry>(
    API_METHOD(
        subghz_txrx_radio_device_set,
        SubGhzRadioDeviceType,
        (SubGhzTxRx*, SubGhzRadioDeviceType)),
    API_METHOD(subghz_txrx_reload_protocol_pack, bool, (SubGhzTxRx*, SubGhzProtocolPackGroup)),
    API_METHOD(subghz_txrx_get_protocol_pack_group, SubGhzProtocolPackGroup, (SubGhzTxRx*)),
    API_METHOD(subghz_txrx_get_protocol_pack_report, const SubGhzProtocolPackReport*, (SubGhzTxRx*)),
    API_METHOD(subghz_txrx_get_setting, SubGhzSetting*, (SubGhzTxRx*)),
    API_METHOD(subghz_txrx_get_preset, SubGhzRadioPreset, (SubGhzTxRx*)),
    API_METHOD(subghz_txrx_radio_device_get, SubGhzRadioDeviceType, (SubGhzTxRx*)),
    API_METHOD(subghz_txrx_radio_device_get_rssi, float, (SubGhzTxRx*)),
    API_METHOD(subghz_txrx_radio_device_is_frequency_valid, bool, (SubGhzTxRx*, uint32_t)),
    API_METHOD(subghz_txrx_analyzer_begin, bool, (SubGhzTxRx*, size_t, uint32_t)),
    API_METHOD(subghz_txrx_analyzer_end, void, (SubGhzTxRx*)),
    API_METHOD(subghz_txrx_set_preset, void, (SubGhzTxRx*, const char*, uint32_t, uint8_t*, size_t)),
    API_METHOD(subghz_file_name_clear, void, (SubGhz*)),
    API_METHOD(subghz_scene_show_unsupported, void, (SubGhz*)),
    API_METHOD(
        subghz_protocol_superrollo_create_data,
        bool,
        (void*, FlipperFormat*, uint32_t, uint8_t, uint16_t, SubGhzRadioPreset*)),
    API_VARIABLE(subghz_protocol_came, const SubGhzProtocol),
    API_VARIABLE(subghz_protocol_nice_flo, const SubGhzProtocol),
    API_VARIABLE(subghz_protocol_bett, const SubGhzProtocol),
    API_VARIABLE(subghz_protocol_marantec24, const SubGhzProtocol),
    API_VARIABLE(subghz_protocol_marantec, const SubGhzProtocol),
    API_VARIABLE(subghz_protocol_gangqi, const SubGhzProtocol),
    API_VARIABLE(subghz_protocol_came_twee, const SubGhzProtocol),
    API_VARIABLE(subghz_protocol_gate_tx, const SubGhzProtocol),
    API_VARIABLE(subghz_protocol_roger, const SubGhzProtocol),
    API_VARIABLE(subghz_protocol_revers_rb2, const SubGhzProtocol),
    API_VARIABLE(subghz_protocol_princeton, const SubGhzProtocol),
    API_VARIABLE(subghz_protocol_telcoma_edge, const SubGhzProtocol),
    API_VARIABLE(subghz_protocol_linear, const SubGhzProtocol),
    API_VARIABLE(subghz_protocol_hollarm, const SubGhzProtocol)));
