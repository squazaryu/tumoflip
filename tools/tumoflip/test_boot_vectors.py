"""Reject link-success images whose F7 Cortex-M vector table cannot boot."""
import struct
import unittest
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
        struct.pack_into("<I", data, 7*4, 0)
        self.validate(data)

    def test_empty_or_truncated_vector_table_is_not_a_firmware(self):
        for data in (b"", self.valid()[:8], self.valid()[:-4], self.valid()+b"\0\0\0\0"):
            with self.subTest(size=len(data)), self.assertRaises(release.ValidationError):
                self.validate(data)

    def test_bad_stack_reset_and_system_handlers_are_rejected(self):
        for index, value in ((0,0), (0,0x20030004), (0,0x08000300), (1,0),
                             (1,0x08000200), (1,0x20000101), (1,0x080D5001),
                             (11,0), (14,0), (15,0x08000001), (20,0x08010000)):
            data=bytearray(self.valid());struct.pack_into("<I",data,index*4,value)
            with self.subTest(index=index,value=hex(value)),self.assertRaises(release.ValidationError):
                self.validate(data)


if __name__ == "__main__":
    unittest.main()
