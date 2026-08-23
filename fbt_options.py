from pathlib import Path
import posixpath

# For more details on these options, run 'fbt -h'

FIRMWARE_ORIGIN = "tumoflip"

# Default hardware target
TARGET_HW = 7

# Optimization flags
## Optimize for size
COMPACT = 1
## Optimize for debugging experience
DEBUG = 0

# Suffix to add to files when building distribution
# If OS environment has DIST_SUFFIX set, it will be used instead
DIST_SUFFIX = "t-dev-007-008"

# Post-update slideshow shown by the updater package
UPDATE_SPLASH = "tumoflip_update"

# Coprocessor firmware
COPRO_OB_DATA = "scripts/ob.data"

# Must match lib/stm32wb_copro version
COPRO_CUBE_VERSION = "1.20.0"

COPRO_CUBE_DIR = "lib/stm32wb_copro"

# Default radio stack
COPRO_STACK_BIN = "stm32wb5x_BLE_Stack_light_fw.bin"
# Firmware also supports "ble_full", but it might not fit into debug builds
COPRO_STACK_TYPE = "ble_light"

# Leave 0 to let scripts automatically calculate it
COPRO_STACK_ADDR = "0x0"

# If you override COPRO_CUBE_DIR on commandline, override this as well
COPRO_STACK_BIN_DIR = posixpath.join(COPRO_CUBE_DIR, "firmware")

# Supported toolchain versions
# Also specify in scripts/ufbt/SConstruct
FBT_TOOLCHAIN_VERSIONS = (" 12.3.", " 13.2.")

OPENOCD_OPTS = [
    "-f",
    "interface/stlink.cfg",
    "-c",
    "transport select hla_swd",
    "-f",
    "${FBT_DEBUG_DIR}/stm32wbx.cfg",
    "-c",
    "stm32wbx.cpu configure -rtos auto",
]

SVD_FILE = "${FBT_DEBUG_DIR}/STM32WB55_CM4.svd"

# Look for blackmagic probe on serial ports and local network
BLACKMAGIC = "auto"

# Application to start on boot
LOADER_AUTOSTART = ""

FIRMWARE_APPS = {
    "default": [
        # Svc
        "basic_services",
        # Apps
        "main_apps",
        "system_apps",
        # Settings
        "settings_apps",
    ],
    "unit_tests": [
        "basic_services",
        "updater_app",
        "radio_device_cc1101_ext",
        "unit_tests",
        "js_app",
        "infrared",
        "archive",
    ],
}

FIRMWARE_APP_SET = "default"

# Local experiments may have an application.fam under applications_user. Keep
# unfinished apps out of reproducible updater packages until explicitly added.
EXCLUDED_EXT_APPS = (
    "js_app",
    "js_badusb",
    "js_blebeacon",
    "js_event_loop",
    "js_gpio",
    "js_gui",
    "js_gui__button_menu",
    "js_gui__button_panel",
    "js_gui__byte_input",
    "js_gui__dialog",
    "js_gui__empty_screen",
    "js_gui__file_picker",
    "js_gui__icon",
    "js_gui__loading",
    "js_gui__menu",
    "js_gui__number_input",
    "js_gui__popup",
    "js_gui__submenu",
    "js_gui__text_box",
    "js_gui__text_input",
    "js_gui__vi_list",
    "js_gui__widget",
    "js_i2c",
    "js_infrared",
    "js_math",
    "js_notification",
    "js_serial",
    "js_spi",
    "js_storage",
    "js_subghz",
    "js_usbdisk",
    "js_vgm",
    "rolljam_standalone",
    "test_js",
    # Dev-only product candidates. Keep source available for continued review,
    # but do not publish their FAPs in the stable release before hardware and
    # security acceptance is complete.
    "tumo_uart_console",
    "tumokey",
)

custom_options_fn = "fbt_options_local.py"

if Path(custom_options_fn).exists():
    exec(compile(Path(custom_options_fn).read_text(), custom_options_fn, "exec"))
