#!/usr/bin/env python3

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]


class LtoBuildContractTest(unittest.TestCase):
    def test_lto_is_scoped_to_the_resident_image(self) -> None:
        commandline = (REPO_ROOT / "site_scons/commandline.scons").read_text(
            encoding="utf-8"
        )
        firmwareopts = (REPO_ROOT / "site_scons/firmwareopts.scons").read_text(
            encoding="utf-8"
        )
        extapps = (REPO_ROOT / "site_scons/extapps.scons").read_text(
            encoding="utf-8"
        )
        options = (REPO_ROOT / "fbt_options.py").read_text(encoding="utf-8")

        self.assertIn('"LTO"', commandline)
        self.assertIn('if ENV["LTO"]:', firmwareopts)
        self.assertIn('CCFLAGS=["-flto"]', firmwareopts)
        self.assertIn('if appenv["LTO"]:', extapps)
        self.assertIn('flag for flag in appenv["CCFLAGS"] if flag != "-flto"', extapps)
        self.assertIn("LTO = 1", options)


if __name__ == "__main__":
    unittest.main()
