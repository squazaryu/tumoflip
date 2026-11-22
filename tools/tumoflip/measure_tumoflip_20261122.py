"""Host LLVM coverage of the November 22 BLE lifetime and read-only Field Tools paths."""

from tools.tumoflip import test_tumoflip_20261122
from tools.tumoflip.measure_upstream_1435_tests import measure

FUNCTIONS = {
    "furi_hal_bt_init",
    "furi_hal_bt_reinit",
    "furi_hal_bt_set_key_storage_change_callback",
    "field_link_status",
    "field_links_report",
    "field_menu_has_item",
    "field_tools_check_links",
}

if __name__ == "__main__":
    raise SystemExit(measure((test_tumoflip_20261122,), FUNCTIONS))
