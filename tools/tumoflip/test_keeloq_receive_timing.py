"""Receive-only timing discrimination on synthetic pulses; no radio or encoder."""
from pathlib import Path
import unittest

from tools.tumoflip.native_optional_memory import native
from tools.tumoflip.test_hotplug_assets import function
from tools.tumoflip.test_upstream_20261105 import KEELOQ_FRAMES, keeloq_declarations

SOURCE = Path(__file__).resolve().parents[2] / "lib/subghz/protocols/keeloq.c"


def receiver():
    source = SOURCE.read_text()
    return (keeloq_declarations(source, ("delta", "preamble")) + "\n" +
            function(source, "void subghz_protocol_decoder_keeloq_reset(") + "\n" +
            function(source, "void subghz_protocol_decoder_keeloq_feed(") + KEELOQ_FRAMES)


class KeeloqReceiveTimingTests(unittest.TestCase):
    def test_sync_dead_zone_does_not_report_other_rate_frames(self):
        native(receiver() + r"""
int main(void) {
    (void)frame;
    const unsigned preambles[] = {200, 250, 400};
    const unsigned gaps[] = {2350, 2400, 2500, 2750, 2999};
    SubGhzKeystore store = {0};
    SubGhzProtocolDecoderKeeloq d = {.base = {.callback = capture}, .keystore = &store};
    expected = UINT64_MAX;
    for(unsigned p = 0; p < COUNT_OF(preambles); ++p) {
        for(unsigned g = 0; g < COUNT_OF(gaps); ++g) {
            subghz_protocol_decoder_keeloq_reset(&d);
            received = 0;
            frame_with_gap(&d, expected, preambles[p], 66, gaps[g]);
            assert(received == 0);
        }
    }
    return 0;
}
""")

    def test_bit_period_must_match_the_locked_rate_in_both_directions(self):
        native(receiver() + r"""
int main(void) {
    (void)frame;
    (void)capture;
    const struct {unsigned te, high, low; bool bit, valid;} cases[] = {
        {200, 200, 400, true, true}, {200, 400, 200, false, true},
        // Half-rate period tolerance is strictly less than 135 us.
        {200, 150, 316, true, true}, {200, 150, 315, true, false},
        {200, 245, 489, true, true}, {200, 246, 489, true, false},
        {200, 316, 150, false, true}, {200, 315, 150, false, false},
        {200, 489, 245, false, true}, {200, 489, 246, false, false},
        // Standard-rate period tolerance is strictly less than 360 us.
        {400, 400, 800, true, true}, {400, 800, 400, false, true},
        {400, 250, 591, true, true}, {400, 250, 590, true, false},
        {400, 591, 250, false, true}, {400, 590, 250, false, false},
    };
    for(unsigned i = 0; i < COUNT_OF(cases); ++i) {
        SubGhzProtocolDecoderKeeloq d = {.te = cases[i].te};
        d.decoder.parser_step = KeeloqDecoderStepCheckDuration;
        d.decoder.te_last = cases[i].high;
        subghz_protocol_decoder_keeloq_feed(&d, false, cases[i].low);
        assert(d.decoder.decode_count_bit == (unsigned)cases[i].valid);
        if(cases[i].valid) {
            assert(d.decoder.parser_step == KeeloqDecoderStepSaveDuration);
            assert(d.decoder.decode_data == (unsigned)cases[i].bit);
        } else {
            assert(d.decoder.parser_step == KeeloqDecoderStepReset);
        }
    }
    return 0;
}
""")

    def test_valid_rates_and_sync_limits_keep_duplicate_and_reset_behavior(self):
        native(receiver() + r"""
int main(void) {
    const unsigned rates[] = {166, 175, 200, 220, 234, 400};
    const uint64_t values[] = {0x0123456789abcdefULL, 0x8000000000000001ULL, UINT64_MAX};
    SubGhzKeystore store = {0};
    SubGhzProtocolDecoderKeeloq d = {.base = {.callback = capture}, .keystore = &store};
    for(unsigned r = 0; r < COUNT_OF(rates); ++r) {
        for(unsigned v = 0; v < COUNT_OF(values); ++v) {
            subghz_protocol_decoder_keeloq_reset(&d);
            received = 0;
            expected = values[v];
            frame(&d, expected, rates[r], 66);
            assert(received == 1);
            frame(&d, expected, rates[r], 66);
            assert(received == 1);
        }
    }
    const struct {unsigned rate, gap; bool valid;} limits[] = {
        {200, 1650, false}, {200, 1651, true}, {200, 2349, true}, {200, 2350, false},
        {400, 2999, false}, {400, 3000, true}, {400, 5799, true}, {400, 5800, false},
    };
    for(unsigned i = 0; i < COUNT_OF(limits); ++i) {
        subghz_protocol_decoder_keeloq_reset(&d);
        received = 0;
        frame_with_gap(&d, expected, limits[i].rate, 64, limits[i].gap);
        assert(received == (unsigned)limits[i].valid);
    }
    FUZZ_DECODER(SubGhzProtocolDecoderKeeloq, subghz_protocol_decoder_keeloq_feed, 4);
    return 0;
}
""")


if __name__ == "__main__":
    unittest.main()
