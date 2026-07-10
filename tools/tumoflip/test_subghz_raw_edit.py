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
            "#define MERGE_GAP_DEFAULT_MS 15",
            "#define MERGE_GAP_MIN_MS 1",
            "#define MERGE_GAP_MAX_MS 32",
            "#define MERGE_REPEAT_DEFAULT 1",
            "#define MERGE_REPEAT_MIN 1",
            "#define MERGE_REPEAT_MAX 64",
        ):
            self.assertIn(definition, source)

        self.assertIn('"Merge gap"', source)
        self.assertIn('"Merge repeat"', source)
        self.assertIn("Merge gap", readme)
        self.assertIn("Merge repeat", readme)

    def test_merge_uses_exact_separator_and_dynamic_path_storage(self) -> None:
        source = (
            REPO_ROOT / "applications_user/subghz_raw_edit/subghz_raw_edit.c"
        ).read_text(encoding="utf-8")

        self.assertIn("static void merge_separator(SubData *dst)", source)
        self.assertIn(
            "dst->data[dst->count - 1] = (int16_t)gap;",
            source,
        )
        self.assertIn("bool skip_lead = add_separator;", source)
        self.assertIn("uint8_t *pbuf = safe_malloc(pcap);", source)
        self.assertIn("uint8_t *np = safe_realloc(pbuf, newcap);", source)
        self.assertIn("free(pbuf);", source)
        self.assertNotIn("MERGE_MAX_FILES", source)

    def test_merge_repeat_is_included_in_capacity_guards(self) -> None:
        source = (
            REPO_ROOT / "applications_user/subghz_raw_edit/subghz_raw_edit.c"
        ).read_text(encoding="utf-8")

        self.assertIn("size_t copies = (size_t)g_merge_repeat;", source)
        self.assertIn("size_t extra = cnt * copies + gaps;", source)
        self.assertIn("bool over_cap = newtotal > MAX_SAMPLES;", source)
        self.assertIn(
            "newtotal * sizeof(int16_t) + LOAD_HEAP_RESERVE > memmgr_get_free_heap()",
            source,
        )

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
