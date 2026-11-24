"""Host-only regression for the ordinary firmware's BLE bond-save path."""

import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
FIXTURE = Path(__file__).with_name("fixtures") / "ble_bond_persistence.c"


def extract(source, pattern, name):
    matches = re.findall(pattern, source, flags=re.MULTILINE | re.DOTALL)
    if len(matches) != 1:
        raise AssertionError(f"Expected one production {name}, found {len(matches)}")
    return matches[0]


def function(source, signature):
    return extract(source, r"^" + re.escape(signature) + r"[^;]*?\{.*?^\}", signature)


def fixture_source(root):
    service_dir = root / "applications/services/bt/bt_service"
    hal = (root / "targets/f7/furi_hal/furi_hal_bt.c").read_text(encoding="utf-8")
    bt = (service_dir / "bt.c").read_text(encoding="utf-8")
    storage = (service_dir / "bt_keys_storage.c").read_text(encoding="utf-8")
    service = function(bt, "int32_t bt_srv(")
    save = function(storage, "static bool bt_keys_storage_save(")
    update_branch = extract(
        service,
        r"else if\(message\.type == BtMessageTypeKeysStorageUpdated\) \{.*?^        \}",
        "key-storage dispatch",
    )
    storage_state = extract(
        storage,
        r"^struct BtKeysStorage \{.*?^\};\s*^typedef struct \{.*?^\} BtKeysStorageFile;",
        "storage layouts",
    )
    constants = extract(
        storage,
        r"(^#define BT_KEYS_STORAGE_MAGIC[^\n]*\n^#define BT_KEYS_STORAGE_VERSION[^\n]*)",
        "storage format constants",
    )
    pieces = {
        "HAL_STATE": extract(
            hal,
            r"^typedef struct \{[^{}]*\} FuriHalBt;\s*^static FuriHalBt furi_hal_bt = \{[^{}]*\};",
            "HAL state",
        ),
        "HAL_FUNCTIONS": "\n".join(
            function(hal, signature)
            for signature in (
                "void furi_hal_bt_init(",
                "void furi_hal_bt_reinit(",
                "void furi_hal_bt_set_key_storage_change_callback(",
            )
        ),
        "KEY_STATE": constants + "\n" + storage_state,
        "KEY_UPDATE": save + "\n" + function(storage, "bool bt_keys_storage_update("),
        "SERVICE_CALLBACK": function(
            bt, "static void bt_on_key_storage_change_callback("
        ),
        "SERVICE_START_APP": function(bt, "static void bt_start_application("),
        "SERVICE_RELOAD": function(bt, "static void bt_handle_reload_keys_settings("),
        "SERVICE_INIT": function(bt, "static void bt_init_keys_settings("),
        "BOOT_BLOCK": extract(
            service,
            r"if\(furi_hal_bt_start_radio_stack\(\)\) \{.*?\n    \}",
            "BLE startup block",
        ),
        "SERVICE_UPDATE_BRANCH": update_branch.removeprefix("else "),
    }
    result = FIXTURE.read_text(encoding="utf-8")
    for name, production_code in pieces.items():
        marker = f"/*__{name}__*/"
        if result.count(marker) != 1:
            raise AssertionError(f"Expected one fixture marker {marker}")
        result = result.replace(marker, production_code)
    return result


class BleBondPersistenceTests(unittest.TestCase):
    def test_bond_changes_reach_saved_struct_across_stack_restart(self):
        with tempfile.TemporaryDirectory(prefix="ble-bond-persistence-") as directory:
            temp = Path(directory)
            source = temp / "fixture.c"
            executable = temp / "fixture"
            source.write_text(fixture_source(ROOT), encoding="utf-8")
            build = subprocess.run(
                [
                    *shlex.split(os.environ.get("CC", "cc")),
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-O1",
                    "-g",
                    "-fsanitize=undefined",
                    "-fno-sanitize-recover=all",
                    str(source),
                    "-o",
                    str(executable),
                ],
                capture_output=True,
                text=True,
                timeout=30,
            )
            self.assertEqual(build.returncode, 0, build.stderr)
            result = subprocess.run(
                [str(executable)], capture_output=True, text=True, timeout=10
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
