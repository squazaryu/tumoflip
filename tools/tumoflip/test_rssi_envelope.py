"""RSSI envelope evidence on synthetic samples only; no radio or TX."""
from pathlib import Path
import unittest

from tools.tumoflip.test_nfc_completion_equality import native

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "applications_user/signal_workbench"


def stats_source():
    header = (APP / "tumospectrum_envelope_stats.h").as_posix()
    implementation = APP / "tumospectrum_envelope_stats.c"
    return (f'#include "{header}"\n#include <assert.h>\n#include <string.h>\n'
            + (implementation.read_text().replace('#include "tumospectrum_envelope_stats.h"', "")
               if implementation.exists() else ""))


class RssiEnvelopeTests(unittest.TestCase):
    def test_weak_overloaded_flat_pulsed_and_ambiguous_windows(self):
        native(stats_source() + r'''
int main(void) {
    int8_t s[TUMO_ENVELOPE_SAMPLES]; TumoEnvelopeWindow w;
    memset(s, -105, sizeof(s));
    assert(tumo_envelope_analyze(s, sizeof(s), -105, &w));
    assert(w.kind == TumoEnvelopeNoSignal);
    memset(s, -60, sizeof(s));
    assert(tumo_envelope_analyze(s, sizeof(s), -105, &w));
    assert(w.kind == TumoEnvelopeFlat && w.transitions == 0);
    for (unsigned i = 0; i < sizeof(s); ++i) s[i] = i % 16 < 8 ? -60 : -105;
    assert(tumo_envelope_analyze(s, sizeof(s), -105, &w));
    assert(w.kind == TumoEnvelopePulsed && w.transitions >= 4);
    for (unsigned i = 0; i < sizeof(s); ++i) s[i] = i % 16 < 8 ? -60 : -83;
    assert(tumo_envelope_analyze(s, sizeof(s), -105, &w));
    assert(w.kind == TumoEnvelopeUnclear); // no quiet-floor evidence
    for (unsigned i = 0; i < sizeof(s); ++i) s[i] = i % 16 < 8 ? -20 : -105;
    assert(tumo_envelope_analyze(s, sizeof(s), -105, &w));
    assert(w.kind == TumoEnvelopeTooStrong);
    memset(s, -105, sizeof(s)); s[40] = -50;
    assert(tumo_envelope_analyze(s, sizeof(s), -105, &w));
    assert(w.kind == TumoEnvelopeNoSignal); // isolated noise spike
    return 0;
}
''')

    def test_invalid_inputs_do_not_read_past_bounds_or_reuse_a_verdict(self):
        native(stats_source() + r'''
int main(void) {
    int8_t s[TUMO_ENVELOPE_SAMPLES] = {0};
    TumoEnvelopeWindow w = {.kind = TumoEnvelopePulsed};
    assert(!tumo_envelope_analyze(NULL, 128, -100, &w));
    assert(w.kind == TumoEnvelopeInvalid);
    assert(!tumo_envelope_analyze(s, 0, -100, &w));
    assert(!tumo_envelope_analyze(s, 15, -100, &w));
    assert(!tumo_envelope_analyze(s, 129, -100, &w));
    assert(!tumo_envelope_analyze(s, 128, -100, NULL));
    assert(!tumo_envelope_analyze(s, 128, -128, &w));
    assert(!tumo_envelope_analyze(s, 128, -100, &w)); // impossible RSSI=0
    memset(s, -60, sizeof(s)); s[127] = -128;
    assert(!tumo_envelope_analyze(s, 128, -100, &w));
    return 0;
}
''')

    def test_bounded_rolling_agreement_never_keeps_an_old_signal(self):
        native(stats_source() + r'''
int main(void) {
    TumoEnvelopeSession s; TumoEnvelopeWindow w = {.kind = TumoEnvelopePulsed};
    tumo_envelope_session_reset(&s);
    for (unsigned i=0; i<5; ++i) tumo_envelope_session_add(&s, &w);
    assert(s.kind == TumoEnvelopeLearning && s.agreement == 0);
    tumo_envelope_session_add(&s, &w);
    assert(s.kind == TumoEnvelopePulsed && s.agreement == 100);
    w.kind = TumoEnvelopeFlat;
    for (unsigned i=0; i<6; ++i) tumo_envelope_session_add(&s, &w);
    assert(s.kind == TumoEnvelopeUnclear && s.agreement == 0);
    for (unsigned i=0; i<100000; ++i) tumo_envelope_session_add(&s, &w);
    assert(s.count == TUMO_ENVELOPE_HISTORY && s.kind == TumoEnvelopeFlat);
    assert(s.agreement == 100);
    w.kind = TumoEnvelopeNoSignal; tumo_envelope_session_add(&s, &w);
    assert(s.kind == TumoEnvelopeNoSignal && !s.agreement && !s.count);
    w.kind = TumoEnvelopeTooStrong; tumo_envelope_session_add(&s, &w);
    assert(s.kind == TumoEnvelopeTooStrong && !s.agreement && !s.count);
    w.kind = TumoEnvelopePulsed; tumo_envelope_session_add(&s, &w);
    assert(s.kind == TumoEnvelopeLearning && !s.agreement);
    assert(!strcmp(tumo_envelope_kind_name(TumoEnvelopeFlat), "Flat carrier"));
    return 0;
}
''')

    def test_random_windows_are_read_only_and_bounded(self):
        native(stats_source() + r'''
int main(void) {
    int8_t samples[128], original[128]; uint32_t rng=0x12345678;
    TumoEnvelopeWindow w;
    for (unsigned trial=0; trial<10000; ++trial) {
        for (unsigned i=0; i<128; ++i) {
            rng = rng * 1664525U + 1013904223U;
            samples[i] = -(int8_t)(1 + (rng % 127));
        }
        memcpy(original, samples, sizeof(samples));
        assert(tumo_envelope_analyze(samples, 128, -105, &w));
        assert(!memcmp(samples, original, sizeof(samples)));
        assert(w.low_dbm <= w.high_dbm && w.transitions < 128);
        assert(w.kind >= TumoEnvelopeNoSignal && w.kind <= TumoEnvelopeTooStrong);
    }
    return 0;
}
''')


if __name__ == "__main__":
    unittest.main()
