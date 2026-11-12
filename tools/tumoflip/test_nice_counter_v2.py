"""Independent vectors for Unleashed c66f01dce7; no radio or key search."""
from pathlib import Path
import unittest
from tools.tumoflip.test_hotplug_assets import function
from tools.tumoflip.test_nfc_completion_equality import native

ROOT = Path(__file__).resolve().parents[2]


class NiceCounterV2Tests(unittest.TestCase):
    def test_decode_is_not_an_involution_and_matches_bit_matrix_vectors(self):
        header = (ROOT / "lib/subghz/protocols/public_api.h").read_text()
        native("#include <stdint.h>\n#include <assert.h>\n" + function(
            header, "static inline uint16_t subghz_protocol_nice_o_cnt_swap(") + r'''
static uint16_t decode_oracle(uint16_t wire) {
    // Independent bit matrix: middle bit of every nibble affects its neighbors.
    uint16_t plain = 0x8b89;
    for (unsigned bit=0; bit<16; ++bit) {
        if (wire & (1u << bit)) {
            plain ^= (uint16_t)(1u << bit);
            if (bit % 4 == 1) plain ^= (uint16_t)((1u << (bit-1)) | (1u << (bit+1)));
        }
    }
    return plain;
}
int main(void) {
    assert(subghz_protocol_nice_o_cnt_swap(0) == 0x8b89);
    assert(subghz_protocol_nice_o_cnt_swap(0xffff) == 0x2123);
    assert(subghz_protocol_nice_o_cnt_swap(0x2222) == 0xfcfe);
    for (unsigned wire=0; wire<65536; ++wire)
        assert(subghz_protocol_nice_o_cnt_swap(wire) == decode_oracle(wire));
    assert(subghz_protocol_nice_o_cnt_swap(subghz_protocol_nice_o_cnt_swap(0)) != 0);
    return 0;
}
''')


if __name__ == "__main__":
    unittest.main()
