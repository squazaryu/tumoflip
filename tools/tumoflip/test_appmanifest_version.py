"""Version values in application manifests must not lose precision."""

import unittest

from scripts.fbt.appmanifest import (
    FlipperApplication,
    FlipperAppType,
    FlipperManifestException,
)


class AppManifestVersionTests(unittest.TestCase):
    def make_app(self, version):
        return FlipperApplication(
            appid="version_test",
            apptype=FlipperAppType.EXTERNAL,
            fap_version=version,
        )

    def test_float_version_is_rejected_with_actionable_error(self):
        with self.assertRaisesRegex(
            FlipperManifestException,
            r"floats are not accepted.*string.*tuple",
        ):
            self.make_app(1.10)

    def test_string_version_preserves_trailing_zero_component(self):
        app = self.make_app("1.10")

        self.assertEqual((1, 10), app.fap_version)

    def test_tuple_version_remains_supported(self):
        app = self.make_app((1, 10))

        self.assertEqual((1, 10), app.fap_version)

    def test_unsupported_version_type_is_rejected_cleanly(self):
        with self.assertRaisesRegex(FlipperManifestException, "expected a string"):
            self.make_app(1)


if __name__ == "__main__":
    unittest.main()
