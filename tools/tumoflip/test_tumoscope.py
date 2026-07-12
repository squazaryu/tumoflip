#!/usr/bin/env python3

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
APP_ROOT = REPO_ROOT / "applications_user/tumoscope"


class TumoScopeTest(unittest.TestCase):
    def test_app_manifest_and_package_contract(self) -> None:
        manifest = (APP_ROOT / "application.fam").read_text(encoding="utf-8")
        validator = (REPO_ROOT / "tools/tumoflip/validate_release.py").read_text(
            encoding="utf-8"
        )
        cockpit = (
            REPO_ROOT / "applications_user/module_one_cockpit/module_one_cockpit.c"
        ).read_text(encoding="utf-8")

        self.assertIn('appid="tumoscope"', manifest)
        self.assertIn('fap_category="Module One/Signals"', manifest)
        self.assertIn(
            'fap_dist_path="apps/Module One/Signals/tumoscope.fap"', manifest
        )
        self.assertIn('fap_icon="icon.png"', manifest)
        self.assertTrue((APP_ROOT / "icon.png").is_file())
        self.assertIn('"apps/Module One/Signals/tumoscope.fap"', validator)
        self.assertIn('"Signals: TumoScope"', cockpit)
        self.assertIn(
            'EXT_PATH("apps/Module One/Signals/tumoscope.fap")', cockpit
        )

    def test_capture_is_dma_bounded_and_restores_gpio(self) -> None:
        source = (APP_ROOT / "tumoscope_capture.c").read_text(encoding="utf-8")
        ring = (APP_ROOT / "tumoscope_ring.c").read_text(encoding="utf-8")
        self.assertIn("LL_DMAMUX_REQ_TIM16_CH1", source)
        self.assertIn("LL_DMA_MODE_CIRCULAR", source)
        self.assertIn("TUMOSCOPE_CAPTURE_MAX_SAMPLES", source)
        self.assertIn("pretrigger_percent", source)
        self.assertIn("post_remaining", ring)
        self.assertIn("tumoscope_ring_trigger_matches", ring)
        self.assertIn("ring->write_index", ring)
        self.assertIn("furi_hal_gpio_init_simple(&gpio_ext_pc0, GpioModeAnalog)", source)
        self.assertIn("furi_hal_gpio_init_simple(&gpio_ext_pc1, GpioModeAnalog)", source)
        self.assertIn("furi_hal_gpio_init_simple(&gpio_ext_pc3, GpioModeAnalog)", source)

    def test_ui_and_export_are_feature_complete(self) -> None:
        source = (APP_ROOT / "tumoscope.c").read_text(encoding="utf-8")
        for expected in (
            "TumoScopeScreenSetup",
            "TumoScopeScreenCapture",
            "TumoScopeScreenWaveform",
            "TumoScopeScreenDecode",
            'profile->demo == TumoScopeDemoNone ? "Capture" : "Run Demo"',
            '"Run Demo"',
            'elements_button_center(canvas, "Stop")',
            'elements_button_center(canvas, "Decode")',
            'elements_button_right(canvas, "Save")',
            '"$timescale 1 ns $end',
            '"HOLD static"',
            '"HOLD @%lu"',
            "tumoscope_analyze_channel",
            "TUMOSCOPE_GRAPH_LEFT",
            "tumoscope_draw_graph_frame",
            "tumoscope_count_bin_transitions",
            "tumoscope_channel_role",
            "tumoscope_wave_visible_samples",
            "tumoscope_wave_max_offset",
            "tumoscope_wave_clamp_offset",
            "fit_samples_per_pixel / 4U",
            "frequency_hz >= 99500U",
            "frequency_hz >= 9950U",
            '"/%s.vcd"',
            '"/%s.txt"',
        ):
            self.assertIn(expected, source)

    def test_decoder_core_with_host_compiler(self) -> None:
        compiler = shutil.which("cc") or shutil.which("clang")
        self.assertIsNotNone(compiler)
        harness_source = r'''
#include "tumoscope_core.h"

#include <assert.h>
#include <string.h>

static void set_channel(uint8_t* samples, size_t start, size_t length, uint8_t channel, bool high) {
    for(size_t i = start; i < start + length; i++) {
        if(high) samples[i] |= 1U << channel;
        else samples[i] &= ~(1U << channel);
    }
}

static void append_level(uint8_t* samples, size_t* count, bool pc0, bool pc1, bool pc3) {
    uint8_t value = (pc0 ? 1U : 0U) | (pc1 ? 2U : 0U) | (pc3 ? 4U : 0U);
    samples[(*count)++] = value;
    samples[(*count)++] = value;
}

int main(void) {
    TumoScopeDecodeResult result;

    uint8_t uart[256];
    memset(uart, 0x07, sizeof(uart));
    const uint8_t uart_value = 0xA5;
    const size_t uart_start = 20;
    set_channel(uart, uart_start, 10, 0, false);
    for(uint8_t bit = 0; bit < 8; bit++) {
        set_channel(uart, uart_start + 10U * (bit + 1U), 10, 0, uart_value & (1U << bit));
    }
    set_channel(uart, uart_start + 90, 10, 0, true);
    tumoscope_decode_uart(uart, sizeof(uart), 96000, 9600, 0, &result);
    assert(result.byte_count == 1);
    assert(result.bytes[0] == uart_value);
    assert(result.error_count == 0);

    uint8_t i2c[256];
    size_t i2c_count = 0;
    append_level(i2c, &i2c_count, true, true, false);
    append_level(i2c, &i2c_count, false, true, false);
    const uint8_t i2c_value = 0xA0;
    for(uint8_t bit = 0; bit < 8; bit++) {
        bool value = i2c_value & (0x80U >> bit);
        append_level(i2c, &i2c_count, value, false, false);
        append_level(i2c, &i2c_count, value, true, false);
    }
    append_level(i2c, &i2c_count, false, false, false);
    append_level(i2c, &i2c_count, false, true, false);
    append_level(i2c, &i2c_count, true, true, false);
    tumoscope_decode_i2c(i2c, i2c_count, 0, 1, &result);
    assert(result.frame_count == 1);
    assert(result.byte_count == 1);
    assert(result.bytes[0] == i2c_value);
    assert(result.secondary[0] == 0);
    assert(result.error_count == 0);

    uint8_t spi[256];
    size_t spi_count = 0;
    const uint8_t mosi = 0x3C;
    const uint8_t miso = 0xA7;
    append_level(spi, &spi_count, false, false, false);
    for(uint8_t bit = 0; bit < 8; bit++) {
        bool out = mosi & (0x80U >> bit);
        bool in = miso & (0x80U >> bit);
        append_level(spi, &spi_count, out, in, false);
        append_level(spi, &spi_count, out, in, true);
        append_level(spi, &spi_count, out, in, false);
    }
    tumoscope_decode_spi_mode0(spi, spi_count, 0, 1, 2, &result);
    assert(result.byte_count == 1);
    assert(result.bytes[0] == mosi);
    assert(result.secondary[0] == miso);
    assert(result.error_count == 0);

    uint8_t one_wire[2048];
    memset(one_wire, 0x07, sizeof(one_wire));
    size_t ow_count = 20;
    set_channel(one_wire, ow_count, 480, 0, false);
    ow_count += 550;
    const uint8_t ow_value = 0xA5;
    for(uint8_t bit = 0; bit < 8; bit++) {
        const size_t low = (ow_value & (1U << bit)) ? 6U : 60U;
        set_channel(one_wire, ow_count, low, 0, false);
        ow_count += low + 70U;
    }
    tumoscope_decode_one_wire(one_wire, ow_count, 1000000, 0, &result);
    assert(result.event_count == 1);
    assert(result.byte_count == 1);
    assert(result.bytes[0] == ow_value);
    assert(result.error_count == 0);

    assert(tumoscope_sample_level(0x05, 0));
    assert(!tumoscope_sample_level(0x05, 1));
    assert(tumoscope_sample_level(0x05, 2));

    uint8_t demo[1024];
    TumoScopeChannelStats stats;
    const uint32_t rates[] = {100000, 250000, 500000, 1000000};
    for(size_t rate_index = 0; rate_index < sizeof(rates) / sizeof(rates[0]); rate_index++) {
        const uint32_t rate = rates[rate_index];
        assert(tumoscope_generate_demo(TumoScopeDemoEdge, demo, sizeof(demo), rate));
        tumoscope_analyze_channel(demo, sizeof(demo), rate, 0, &stats);
        assert(stats.frequency_hz == 5000);
        assert(stats.transitions >= 9);
        tumoscope_analyze_channel(demo, sizeof(demo), rate, 1, &stats);
        assert(stats.frequency_hz == 3125);
        tumoscope_analyze_channel(demo, sizeof(demo), rate, 2, &stats);
        assert(stats.frequency_hz == 2500);
    }

    memset(demo, 0, sizeof(demo));
    tumoscope_analyze_channel(demo, sizeof(demo), 100000, 0, &stats);
    assert(stats.frequency_hz == 0);
    assert(stats.transitions == 0);

    for(size_t rate_index = 0; rate_index < sizeof(rates) / sizeof(rates[0]); rate_index++) {
        const uint32_t rate = rates[rate_index];
        const uint32_t baud = tumoscope_demo_uart_baud(rate);
        assert(tumoscope_generate_demo(TumoScopeDemoUart, demo, sizeof(demo), rate));
        tumoscope_decode_uart(demo, sizeof(demo), rate, baud, 0, &result);
        assert(result.byte_count >= 3);
        assert(result.bytes[0] == 0x55);
        assert(result.bytes[1] == 0xA5);
        assert(result.bytes[2] == 0x33);
        assert(result.error_count == 0);
    }

    assert(tumoscope_generate_demo(TumoScopeDemoI2c, demo, sizeof(demo), 100000));
    tumoscope_decode_i2c(demo, sizeof(demo), 0, 1, &result);
    assert(result.byte_count > 0);
    assert(result.bytes[0] == 0xA0);
    assert(result.error_count == 0);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            harness = Path(directory) / "tumoscope_test.c"
            binary = Path(directory) / "tumoscope_test"
            harness.write_text(harness_source, encoding="utf-8")
            subprocess.run(
                [
                    compiler,
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    f"-I{APP_ROOT}",
                    str(harness),
                    str(APP_ROOT / "tumoscope_core.c"),
                    "-o",
                    str(binary),
                ],
                check=True,
                cwd=REPO_ROOT,
            )
            subprocess.run([str(binary)], check=True)

    def test_trigger_ring_with_host_compiler(self) -> None:
        compiler = shutil.which("cc") or shutil.which("clang")
        self.assertIsNotNone(compiler)
        harness_source = r'''
#include "tumoscope_ring.h"

#include <assert.h>
#include <string.h>

int main(void) {
    uint8_t storage[8];
    uint8_t output[8];
    TumoScopeRing ring;

    tumoscope_ring_init(&ring, storage, 4, TumoScopeTriggerAuto, 25);
    assert(!tumoscope_ring_push(&ring, 1));
    assert(!tumoscope_ring_push(&ring, 2));
    assert(!tumoscope_ring_push(&ring, 3));
    assert(tumoscope_ring_push(&ring, 4));
    assert(tumoscope_ring_count(&ring) == 4);
    assert(tumoscope_ring_progress(&ring) == 4);
    assert(tumoscope_ring_copy(&ring, output, sizeof(output)));
    assert(memcmp(output, (uint8_t[]){1, 2, 3, 4}, 4) == 0);
    assert(ring.trigger_index == 0);

    tumoscope_ring_init(&ring, storage, 8, TumoScopeTriggerPc0Rising, 25);
    for(size_t index = 0; index < 5; index++) {
        assert(!tumoscope_ring_push(&ring, 0));
    }
    assert(!tumoscope_ring_push(&ring, 1));
    for(size_t index = 0; index < 4; index++) {
        assert(!tumoscope_ring_push(&ring, 1));
    }
    assert(tumoscope_ring_push(&ring, 1));
    assert(tumoscope_ring_count(&ring) == 8);
    assert(tumoscope_ring_copy(&ring, output, sizeof(output)));
    assert(memcmp(output, (uint8_t[]){0, 0, 1, 1, 1, 1, 1, 1}, 8) == 0);
    assert(ring.trigger_index == 2);

    tumoscope_ring_init(&ring, storage, 8, TumoScopeTriggerPc3Falling, 25);
    assert(!tumoscope_ring_push(&ring, 4));
    assert(!tumoscope_ring_push(&ring, 4));
    assert(!tumoscope_ring_push(&ring, 4));
    assert(!tumoscope_ring_push(&ring, 0));
    for(size_t index = 0; index < 4; index++) {
        assert(!tumoscope_ring_push(&ring, 0));
    }
    assert(tumoscope_ring_push(&ring, 0));
    assert(tumoscope_ring_copy(&ring, output, sizeof(output)));
    assert(memcmp(output, (uint8_t[]){4, 4, 0, 0, 0, 0, 0, 0}, 8) == 0);
    assert(ring.trigger_index == 2);

    tumoscope_ring_init(&ring, storage, 8, TumoScopeTriggerPc1High, 25);
    assert(!tumoscope_ring_push(&ring, 0));
    assert(!tumoscope_ring_push(&ring, 0));
    assert(!tumoscope_ring_push(&ring, 2));
    for(size_t index = 0; index < 4; index++) {
        assert(!tumoscope_ring_push(&ring, 2));
    }
    assert(tumoscope_ring_push(&ring, 2));
    assert(tumoscope_ring_copy(&ring, output, sizeof(output)));
    assert(memcmp(output, (uint8_t[]){0, 0, 2, 2, 2, 2, 2, 2}, 8) == 0);
    assert(ring.trigger_index == 2);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            harness = Path(directory) / "tumoscope_ring_test.c"
            binary = Path(directory) / "tumoscope_ring_test"
            harness.write_text(harness_source, encoding="utf-8")
            subprocess.run(
                [
                    compiler,
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    f"-I{APP_ROOT}",
                    str(harness),
                    str(APP_ROOT / "tumoscope_ring.c"),
                    "-o",
                    str(binary),
                ],
                check=True,
                cwd=REPO_ROOT,
            )
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
