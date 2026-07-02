#!/usr/bin/env python3

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]


class NetworkGpsRpcGateTest(unittest.TestCase):
    def test_network_and_gps_services_are_not_started_without_companion_contract(self) -> None:
        services_manifest = (
            REPO_ROOT / "applications/services/application.fam"
        ).read_text(encoding="utf-8")
        rpc_source = (REPO_ROOT / "applications/services/rpc/rpc.c").read_text(
            encoding="utf-8"
        )
        rpc_header = (REPO_ROOT / "applications/services/rpc/rpc_i.h").read_text(
            encoding="utf-8"
        )

        self.assertNotIn('"gps_start"', services_manifest)
        self.assertNotIn('"network_start"', services_manifest)
        self.assertNotIn("rpc_gps_alloc", rpc_source + rpc_header)
        self.assertNotIn("rpc_network_alloc", rpc_source + rpc_header)

    def test_network_and_gps_api_exports_are_not_published_yet(self) -> None:
        api_symbols = (REPO_ROOT / "targets/f7/api_symbols.csv").read_text(
            encoding="utf-8"
        )

        for forbidden in (
            "applications/services/gps/gps.h",
            "applications/services/network/network.h",
            "gps_request_stream",
            "gps_request_location",
            "gps_report_location",
            "network_connect",
            "network_http_request",
            "network_websocket_open",
        ):
            self.assertNotIn(forbidden, api_symbols)

    def test_upstream_service_trees_are_not_partially_imported(self) -> None:
        for relative in (
            "applications/services/gps",
            "applications/services/network",
            "applications/services/rpc/rpc_gps.c",
            "applications/services/rpc/rpc_network.c",
        ):
            self.assertFalse((REPO_ROOT / relative).exists(), relative)

    def test_gate_is_documented(self) -> None:
        docs = (REPO_ROOT / "docs/network-gps-rpc-gate.md").read_text(
            encoding="utf-8"
        )

        for required in (
            "687375d7b",
            "companion-side design",
            "privacy boundaries",
            "Do not enable",
            "gps_start",
            "network_start",
            "rpc_gps",
            "rpc_network",
        ):
            self.assertIn(required, docs)


if __name__ == "__main__":
    unittest.main()
