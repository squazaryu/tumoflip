"""Regression contracts for the built-in KeyFinder2 and Doorbell32 sensors."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


class SubGhzSensorProtocolsTests(unittest.TestCase):
    def test_keyfinder2_and_doorbell32_are_static_433_am_sensor_protocols(self):
        protocols = {
            "keyfinder2": ("subghz_protocol_keyfinder2", "KeyFinder2"),
            "doorbell32": ("subghz_protocol_doorbell32", "Doorbell32"),
        }
        registry_c = read("lib/subghz/protocols/protocol_items.c")
        registry_h = read("lib/subghz/protocols/protocol_items.h")
        receiver_config = read("applications/main/subghz/scenes/subghz_scene_receiver_config.c")

        self.assertIn('"Ignore Sensors"', receiver_config)
        self.assertIn("SubGhzProtocolFlag_Sensors", receiver_config)

        for filename, (symbol, display_name) in protocols.items():
            with self.subTest(protocol=filename):
                source = read(f"lib/subghz/protocols/{filename}.c")
                header = read(f"lib/subghz/protocols/{filename}.h")
                entry = source.split(f"const SubGhzProtocol {symbol} =", 1)[1].split("};", 1)[0]

                self.assertIn(f'"{display_name}"', header)
                self.assertEqual(registry_c.count(f"&{symbol}"), 1)
                self.assertIn(f'#include "{filename}.h"', registry_h)
                self.assertIn("SubGhzProtocolTypeStatic", entry)
                for flag in (
                    "SubGhzProtocolFlag_433",
                    "SubGhzProtocolFlag_AM",
                    "SubGhzProtocolFlag_Decodable",
                    "SubGhzProtocolFlag_Load",
                    "SubGhzProtocolFlag_Save",
                    "SubGhzProtocolFlag_Send",
                    "SubGhzProtocolFlag_Sensors",
                ):
                    self.assertIn(flag, entry)

    def test_protocol_documentation_names_sensor_use_and_wire_format(self):
        supported = read("documentation/SubGHzSupportedSystems.md")
        self.assertRegex(supported, r"KeyFinder2 .*433\.92MHz.*11 bits, Static")
        self.assertRegex(supported, r"Doorbell32 .*433\.92MHz.*32 bits, Static")
        self.assertIn("14*Te guard", supported)


if __name__ == "__main__":
    unittest.main()
