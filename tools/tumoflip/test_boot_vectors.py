"""Host-only startup-vector checks, independent of rejected 005 runtime code."""
from pathlib import Path
import struct
import unittest
from unittest.mock import patch

from tools.tumoflip import validate_release as release


class BootVectorTests(unittest.TestCase):
    def validate(self, data):
        validator = getattr(release, "validate_c1_vector_data", None)
        self.assertIsNotNone(validator, "Missing F7 startup-vector validation")
        return validator(data, 0x08000140, 0x080D5000)

    def valid(self):
        return struct.pack("<79I", 0x20030000, *([0x08000201] * 78))

    def test_bootable_table_and_reserved_entries(self):
        data = bytearray(self.valid())
        struct.pack_into("<I", data, 7 * 4, 0)
        self.validate(data)

    def test_empty_or_truncated_vector_table_is_rejected(self):
        for data in (b"", self.valid()[:8], self.valid()[:-4], self.valid() + b"\0" * 4):
            with self.subTest(size=len(data)), self.assertRaises(release.ValidationError):
                self.validate(data)

    def test_bad_stack_reset_and_system_handlers_are_rejected(self):
        cases = ((0, 0), (0, 0x20030004), (0, 0x08000300), (1, 0),
                 (1, 0x08000200), (1, 0x20000101), (1, 0x080D5001),
                 (11, 0), (14, 0), (15, 0x08000001), (20, 0x08010000))
        for index, value in cases:
            data = bytearray(self.valid())
            struct.pack_into("<I", data, index * 4, value)
            with self.subTest(index=index, value=hex(value)), self.assertRaises(release.ValidationError):
                self.validate(data)

    def elf_adapter(self, sections, data):
        validator = getattr(release, "validate_boot_vectors", None)
        self.assertIsNotNone(validator, "Missing ELF-to-vector validation adapter")

        def extract(args, **kwargs):
            self.assertEqual(args[:4], ["/fixture/arm-none-eabi-objcopy", "-O", "binary",
                                       "--only-section=.isr_vector"])
            self.assertEqual(args[4], "fixture.elf")
            self.assertTrue(kwargs["check"])
            Path(args[5]).write_bytes(data)

        with patch.object(release, "find_objdump", return_value=Path("/fixture/arm-none-eabi-objdump")), \
                patch.object(release.subprocess, "check_output", return_value=sections), \
                patch.object(release.subprocess, "run", side_effect=extract):
            validator(Path("/fixture"), Path("fixture.elf"))

    def test_elf_adapter_checks_extracted_bytes_not_only_section_names(self):
        sections = ("0 .isr_vector 0000013c 08000000\n CONTENTS, ALLOC, LOAD\n"
                    "1 .text 00001000 08000140\n CONTENTS, ALLOC, LOAD\n")
        self.elf_adapter(sections, self.valid())
        invalid = bytearray(self.valid())
        struct.pack_into("<I", invalid, 4, 0)
        with self.assertRaises(release.ValidationError):
            self.elf_adapter(sections, invalid)

    def test_elf_adapter_rejects_missing_unloaded_or_misplaced_sections(self):
        cases = (
            "",
            "0 .isr_vector 0000013c 08000000\n CONTENTS, ALLOC\n1 .text 00001000 08000140\n LOAD\n",
            "0 .isr_vector 0000013c 08000004\n LOAD\n1 .text 00001000 08000140\n LOAD\n",
            "0 .isr_vector 00000138 08000000\n LOAD\n1 .text 00001000 08000140\n LOAD\n",
            "0 .isr_vector 0000013c 08000000\n LOAD\n",
        )
        for sections in cases:
            with self.subTest(sections=sections), self.assertRaises(release.ValidationError):
                self.elf_adapter(sections, self.valid())


if __name__ == "__main__":
    unittest.main()
