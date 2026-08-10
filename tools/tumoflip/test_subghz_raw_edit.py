#!/usr/bin/env python3

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]


class SubGhzRawEditTest(unittest.TestCase):
    def test_raw_edit_is_visible_arf_tool(self) -> None:
        manifest = (
            REPO_ROOT / "applications_user/subghz_raw_edit/application.fam"
        ).read_text(encoding="utf-8")

        self.assertIn('appid="subghz_raw_edit"', manifest)
        self.assertIn('name="Sub-GHz RAW Edit"', manifest)
        self.assertIn('fap_category="ARF Tools"', manifest)
        self.assertIn("Lechnio; tumoflip integration", manifest)
        self.assertIn('fap_version="1.7"', manifest)
        self.assertNotIn("fap_dist_path", manifest)

    def test_raw_edit_is_source_only_import(self) -> None:
        app_dir = REPO_ROOT / "applications_user/subghz_raw_edit"
        forbidden_suffixes = {".fap", ".fal", ".elf", ".dfu", ".bin"}
        found = [
            path.relative_to(REPO_ROOT).as_posix()
            for path in app_dir.rglob("*")
            if path.is_file() and path.suffix.lower() in forbidden_suffixes
        ]

        self.assertEqual(found, [])

    def test_raw_edit_documented_as_receive_analysis_only(self) -> None:
        readme = (REPO_ROOT / "applications_user/subghz_raw_edit/README.md").read_text(
            encoding="utf-8"
        )
        source = (
            REPO_ROOT / "applications_user/subghz_raw_edit/subghz_raw_edit.c"
        ).read_text(encoding="utf-8")

        self.assertIn("Receive/analysis only", readme)
        self.assertIn("This app never transmits", readme)
        self.assertNotIn("furi_hal_subghz_tx", source)
        self.assertNotIn("subghz_txrx_tx_start", source)

    def test_merge_controls_are_bounded_and_documented(self) -> None:
        readme = (REPO_ROOT / "applications_user/subghz_raw_edit/README.md").read_text(
            encoding="utf-8"
        )
        source = (
            REPO_ROOT / "applications_user/subghz_raw_edit/subghz_raw_edit.c"
        ).read_text(encoding="utf-8")

        for definition in (
            "#define MERGE_GAP_DEFAULT_MS 100",
            "#define MERGE_GAP_MIN_MS 0",
            "#define MERGE_GAP_MAX_MS 1000",
            "#define MERGE_REPEAT_DEFAULT 1",
            "#define MERGE_REPEAT_MIN 1",
            "#define MERGE_REPEAT_MAX 64",
        ):
            self.assertIn(definition, source)

        self.assertIn('"Merge gap"', source)
        self.assertIn('"Merge repeat"', source)
        self.assertIn("Merge gap", readme)
        self.assertIn("Merge repeat", readme)
        self.assertIn("0-1000 ms", readme)

    def test_merge_controls_use_bounded_number_input(self) -> None:
        source = (
            REPO_ROOT / "applications_user/subghz_raw_edit/subghz_raw_edit.c"
        ).read_text(encoding="utf-8")

        self.assertIn("#include <gui/modules/number_input.h>", source)
        self.assertIn("NumberInput *num_input;", source)
        self.assertIn("number_input_alloc()", source)
        self.assertIn("number_input_set_result_callback(", source)
        self.assertIn("current, min_value, max_value", source)
        self.assertIn("number_input_free(menu->num_input)", source)

    def test_merge_preserves_long_and_native_gaps(self) -> None:
        source = (
            REPO_ROOT / "applications_user/subghz_raw_edit/subghz_raw_edit.c"
        ).read_text(encoding="utf-8")

        self.assertIn("static bool push_duration(SubData *sd, int32_t value)", source)
        self.assertIn("static size_t duration_samples(int32_t value)", source)
        self.assertIn("static bool merge_separator(SubData *dst)", source)
        self.assertIn("MergeJoinManual", source)
        self.assertIn("MergeJoinNative", source)
        self.assertIn("if (g_merge_gap_ms == 0)\n        return true;", source)
        self.assertIn("has_previous && g_merge_gap_ms > 0", source)
        self.assertIn("return push_duration(dst, -(g_merge_gap_ms * 1000));", source)
        self.assertNotIn("(int16_t)gap", source)

        clamp = 32000
        for milliseconds, expected_chunks in ((1, 1), (100, 4), (1000, 32)):
            microseconds = milliseconds * 1000
            chunks = (microseconds + clamp - 1) // clamp
            self.assertEqual(chunks, expected_chunks)

    def test_merge_uses_dynamic_path_storage(self) -> None:
        source = (
            REPO_ROOT / "applications_user/subghz_raw_edit/subghz_raw_edit.c"
        ).read_text(encoding="utf-8")

        self.assertIn("uint8_t *pbuf = safe_malloc(pcap);", source)
        self.assertIn("uint8_t *np = safe_realloc(pbuf, newcap);", source)
        self.assertIn("free(pbuf);", source)
        self.assertNotIn("MERGE_MAX_FILES", source)

    def test_merge_repeat_is_included_in_capacity_guards(self) -> None:
        source = (
            REPO_ROOT / "applications_user/subghz_raw_edit/subghz_raw_edit.c"
        ).read_text(encoding="utf-8")

        self.assertIn("size_t copies = (size_t)g_merge_repeat;", source)
        self.assertIn("static bool checked_size_add", source)
        self.assertIn("static bool checked_size_mul", source)
        self.assertIn("static bool merge_estimate_samples", source)
        self.assertIn("static bool merge_peak_fits_heap", source)
        self.assertIn("next_temporary_samples", source)
        self.assertIn("return ok && appended_signal;", source)
        self.assertIn("No output was saved.", source)

    def test_release_and_deploy_include_raw_edit_as_visible_app(self) -> None:
        validate = (REPO_ROOT / "tools/tumoflip/validate_release.py").read_text(
            encoding="utf-8"
        )
        deploy = (REPO_ROOT / "tools/tumoflip/deploy_module_one_apps.py").read_text(
            encoding="utf-8"
        )
        docs = (REPO_ROOT / "docs/arf-subghz-full.md").read_text(encoding="utf-8")

        self.assertIn('"subghz_raw_edit"', validate)
        self.assertIn("subghz_raw_edit.fap", deploy)
        self.assertIn("Sub-GHz RAW Edit", docs)


if __name__ == "__main__":
    unittest.main()
