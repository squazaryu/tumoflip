#!/usr/bin/env python3
"""Regression contracts for the upstream power and plugin-scan fixes."""

import re
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]


def function_body(source: str, name: str, next_name: str | None = None) -> str:
    start = source.index(name)
    if next_name is not None:
        end = source.index(next_name, start)
    else:
        end = len(source)
    return source[start:end]


class UpstreamPowerAndPluginScanTest(unittest.TestCase):
    def test_power_init_reports_the_actual_controller_results(self) -> None:
        source = (
            REPO_ROOT / "targets/f7/furi_hal/furi_hal_power.c"
        ).read_text(encoding="utf-8")
        init = function_body(source, "void furi_hal_power_init", "bool furi_hal_power_gauge_is_ok")

        self.assertIn(
            "if(furi_hal_power.gauge_ok && furi_hal_power.charger_ok)",
            init,
        )
        self.assertIn('FURI_LOG_I(TAG, "Init OK");', init)
        self.assertIn('"Init failed: gauge %u, charger %u"', init)
        self.assertIn("furi_hal_power.gauge_ok,", init)
        self.assertIn("furi_hal_power.charger_ok);", init)

    def test_plugin_scan_keeps_loading_after_one_bad_file(self) -> None:
        source = (
            REPO_ROOT / "lib/flipper_application/plugins/plugin_manager.c"
        ).read_text(encoding="utf-8")
        scan = function_body(
            source,
            "plugin_manager_load_all_internal",
            "PluginManagerError plugin_manager_load_all(",
        )

        self.assertIn("PluginManagerError result = PluginManagerErrorNone;", scan)
        self.assertRegex(scan, re.compile(r"while\(storage_dir_read\("))
        self.assertIn("plugin_manager_load_single_detailed_internal", scan)
        self.assertIn("PluginManagerLoadStatusApplicationIdMismatch", scan)
        self.assertIn("storage_file_get_error(directory)", scan)
        self.assertIn("FSE_NOT_EXIST", scan)
        self.assertIn("return result;", scan)
        self.assertNotIn("break;\n        }\n    } while(false);", scan)

    def test_subghz_registry_reports_partial_device_scans(self) -> None:
        source = (
            REPO_ROOT / "lib/subghz/devices/registry.c"
        ).read_text(encoding="utf-8")
        types = (REPO_ROOT / "lib/subghz/devices/types.h").read_text(encoding="utf-8")

        self.assertIn('#define SUBGHZ_RADIO_DEVICE_PLUGIN_FAL_PREFIX  "radio_device_"', types)
        self.assertIn("PluginManagerError error = plugin_manager_load_all_with_prefix(", source)
        self.assertIn("plugin_manager_get_count(subghz_device->manager)", source)
        self.assertIn('"Failed to load radio device plugin(s), error %d"', source)
        self.assertIn('"No " SUBGHZ_RADIO_DEVICE_PLUGIN_FAL_PREFIX "*.fal plugins found"', source)

    def test_plugin_examples_do_not_abort_before_freeing_the_manager(self) -> None:
        for relative in (
            "applications/examples/example_plugins/example_plugins_multi.c",
            "applications/examples/example_plugins_advanced/example_advanced_plugins.c",
        ):
            source = (REPO_ROOT / relative).read_text(encoding="utf-8")
            load_start = source.index("plugin_manager_load_all(")
            failure_start = source.rfind("if(", 0, load_start)
            failure_end = source.index("uint32_t plugin_count", failure_start)
            failure_path = source[failure_start:failure_end]
            self.assertNotIn("return 0;", failure_path)
            self.assertIn("plugin_manager_free(manager);", source[failure_end:])


if __name__ == "__main__":
    unittest.main()
