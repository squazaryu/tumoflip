#!/usr/bin/env python3

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
APP_ROOT = REPO_ROOT / "applications_user/tumokey"
SOURCE_ROOT = APP_ROOT / "src"


class TumoKeyTest(unittest.TestCase):
    def test_stable_release_excludes_unaccepted_security_app(self) -> None:
        options = (REPO_ROOT / "fbt_options.py").read_text(encoding="utf-8")
        cockpit = (
            REPO_ROOT
            / "applications_user/module_one_cockpit/module_one_cockpit.c"
        ).read_text(encoding="utf-8")
        validator = (
            REPO_ROOT / "tools/tumoflip/validate_release.py"
        ).read_text(encoding="utf-8")

        self.assertIn('"tumokey"', options)
        self.assertNotIn('"Security: TumoKey"', cockpit)
        self.assertNotIn('"apps/Module One/Security/tumokey.fap"', validator)

    def test_manifest_is_dev_only_api_88_package(self) -> None:
        manifest = (APP_ROOT / "application.fam").read_text(encoding="utf-8")

        for required in (
            'appid="tumokey"',
            'name="TumoKey"',
            'fap_category="Module One/Security"',
            'fap_dist_path="apps/Module One/Security/tumokey.fap"',
            'fap_version="0.9.0"',
            "ZF_RELEASE_DIAGNOSTICS=0",
            "ZF_AUTO_ACCEPT_REQUESTS=0",
            "ZF_DEV_FIDO2_1=0",
            "ZF_PACKED_ATTESTATION=1",
            "Dev-only FIDO2/U2F authenticator",
        ):
            self.assertIn(required, manifest)

    def test_upstream_provenance_is_pinned(self) -> None:
        upstream = (APP_ROOT / "UPSTREAM.md").read_text(encoding="utf-8")
        notices = (APP_ROOT / "THIRD_PARTY_NOTICES.md").read_text(encoding="utf-8")

        self.assertIn("https://github.com/MinorGlitch/zerofido", upstream)
        self.assertIn("56b433f9d1390ad224246c85a06083b40de4903e", upstream)
        self.assertIn("GPL-3.0-or-later", upstream)
        self.assertIn("Alex Stoyanov", upstream)
        self.assertIn("ZeroFIDO", notices)

    def test_storage_namespace_and_product_ui_are_tumokey(self) -> None:
        types = (SOURCE_ROOT / "zerofido_types.h").read_text(encoding="utf-8")
        status = (SOURCE_ROOT / "ui/status.c").read_text(encoding="utf-8")
        views = (SOURCE_ROOT / "ui/views.c").read_text(encoding="utf-8")

        self.assertIn('#define ZF_APP_ID "tumokey"', types)
        self.assertIn('"TumoKey"', status)
        self.assertIn('"Guide / About"', views)
        self.assertIn('"TumoKey 0.9 dev\\n"', views)
        self.assertIn("Do not use as your only authenticator", views)

    def test_required_protocol_surfaces_are_present(self) -> None:
        required_files = (
            "ctap/commands/make_credential.c",
            "ctap/commands/get_assertion.c",
            "pin/command.c",
            "transport/usb_hid_worker.c",
            "transport/nfc_worker.c",
            "u2f/session.c",
            "store/recovery.c",
        )
        for relative in required_files:
            self.assertTrue((SOURCE_ROOT / relative).is_file(), relative)

    def test_runtime_does_not_crash_on_host_controlled_values(self) -> None:
        runtime = "\n".join(
            path.read_text(encoding="utf-8", errors="replace")
            for path in SOURCE_ROOT.rglob("*")
            if path.suffix in {".c", ".h"}
        )

        self.assertNotIn("furi_check(", runtime)
        self.assertNotIn("furi_crash(", runtime)

    def test_native_regression_harness_is_vendored(self) -> None:
        self.assertTrue((APP_ROOT / "tools/run_protocol_regressions.py").is_file())
        self.assertTrue((APP_ROOT / "tests/native/protocol/runner.c").is_file())
        self.assertTrue((APP_ROOT / "tests/native/transport_u2f/runner.c").is_file())

    def test_host_crypto_dependency_is_security_pinned(self) -> None:
        requirements = (APP_ROOT / "host_tools/requirements.txt").read_text(
            encoding="utf-8"
        )
        self.assertRegex(requirements, r"(?m)^cryptography==50\.0\.1$")


if __name__ == "__main__":
    unittest.main()
