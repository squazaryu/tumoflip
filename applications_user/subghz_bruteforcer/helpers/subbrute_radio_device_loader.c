#include "subbrute_radio_device_loader.h"

#include <furi/core/check.h>
#include <lib/subghz/devices/cc1101_int/cc1101_int_interconnect.h>

#include "applications/drivers/subghz/cc1101_ext/cc1101_ext_interconnect.h"

#define TAG "SubBruteRadioDeviceLoader"

/**
 * @brief This function is responsible for powering on the radio device loader.
 *
 * This function is responsible for powering on the radio device loader.
 * It attempts to enable the OTG (On-The-Go) power.
 * The function uses a @c `while` loop with a maximum of 5 attempts to enable the OTG power.
 * If it succeeds, it will break out of the loop.
 * If the maximum number of attempts is reached and the OTG power is not enabled,
 * the function checks the USB voltage using @c `furi_hal_power_get_usb_voltage()`.
 *
 * If the voltage is below 4.5 volts, an error message is logged using @c `FURI_LOG_E()`
 * with a message includes the value of @c `furi_hal_power_check_otg_fault()`
 * (converted to 1 if true, 0 if false),
 * which checks the OTG fault status using the BQ2589 chip.
 *
*/
static bool subbrute_radio_device_loader_power_on(
    SubGhzRadioBroker* broker,
    const SubGhzRadioBrokerLease* lease) {
    return subghz_radio_broker_external_power_on(broker, lease);
}

/**
 * @brief Turns off the power of the radio device loader.
 *
 * This function checks if OTG (On-The-Go) power is enabled, and if so, it disables it.
 *
 * @note This function is static and used internally by the radio device loader module.
 *
 */
static void subbrute_radio_device_loader_power_off(
    SubGhzRadioBroker* broker,
    const SubGhzRadioBrokerLease* lease) {
    subghz_radio_broker_external_power_off(broker, lease);
}

/**
 * @brief Checks if a radio device of the given name is connected externally.
 *
 * This function checks if a radio device with the specified name is connected externally.
 * If the OTG power is not enabled, it attempts to power on the radio device loader.
 * It then retrieves the device information using the provided name and checks if it is connected.
 * Finally, if the OTG power was initially disabled, it powers off the radio device loader.
 *
 * @param[in] name The name of the radio device to check.
 *
 * @return true if the device is connected externally, false otherwise.
 *
 */
static bool subbrute_radio_device_loader_is_connect_external(
    const char* name,
    SubGhzRadioBroker* broker,
    const SubGhzRadioBrokerLease* lease) {
    bool is_connect = false;
    const bool powered = subbrute_radio_device_loader_power_on(broker, lease);

    const SubGhzDevice* device = powered ? subghz_devices_get_by_name(name) : NULL;
    if(device) {
        is_connect = subghz_devices_is_connect(device);
    }

    subbrute_radio_device_loader_power_off(broker, lease);

    return is_connect;
}

const SubGhzDevice* subbrute_radio_device_loader_set(
    const SubGhzDevice* current_radio_device,
    SubGhzRadioDeviceType radio_device_type,
    SubGhzRadioBroker* broker,
    const SubGhzRadioBrokerLease* lease) {
    const SubGhzDevice* radio_device;

    if(radio_device_type == SubGhzRadioDeviceTypeExternalCC1101 &&
       subbrute_radio_device_loader_is_connect_external(
           SUBGHZ_DEVICE_CC1101_EXT_NAME, broker, lease)) {
        subbrute_radio_device_loader_power_on(broker, lease);
        radio_device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_EXT_NAME);
        subghz_devices_begin(radio_device);
    } else if(current_radio_device == NULL) {
        radio_device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);
    } else {
        subbrute_radio_device_loader_end(current_radio_device, broker, lease);
        radio_device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);
    }

    return radio_device;
}

void subbrute_radio_device_loader_end(
    const SubGhzDevice* radio_device,
    SubGhzRadioBroker* broker,
    const SubGhzRadioBrokerLease* lease) {
    furi_assert(radio_device);

    subbrute_radio_device_loader_power_off(broker, lease);
    if(radio_device != subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME)) {
        subghz_devices_end(radio_device);
    }
}
