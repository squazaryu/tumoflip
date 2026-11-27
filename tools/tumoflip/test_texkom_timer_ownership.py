"""Execute the receive window and real speaker ownership functions off-device."""
from pathlib import Path
import re
import unittest

from tools.tumoflip.native_optional_memory import native
from tools.tumoflip.test_hotplug_assets import function

ROOT = Path(__file__).resolve().parents[2]
HAL = ROOT / "targets/f7/furi_hal"

HARNESS = r"""
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#define furi_check assert
#define FURI_IS_IRQ_MODE() false
#define FURI_LOG_D(...) ((void)0)
typedef unsigned FuriThreadId;
typedef int FuriMutex;
typedef int SignalReader;
typedef int FuriHalSpiBusHandle;
typedef enum { FuriHalNfcErrorNone, FuriHalNfcErrorCommunicationTimeout,
               FuriHalNfcErrorBufferOverflow } FuriHalNfcError;
enum { FuriStatusOk, FuriStatusErrorResource, FuriHalBusTIM16 = 16,
       GpioModeAltFunctionPushPull, GpioModeAnalog, GpioPullNo, GpioPullDown,
       GpioSpeedLow, GpioAltFn14TIM16,
       FuriHalNfcEventInternalTypeTransparentDataReceived = 128 };
#define BITS_IN_BYTE 8U
#define FURI_HAL_SPEAKER_TIMER 16
static const int gpio_speaker = 1;
static unsigned current_thread = 1, mutex_owner, insomnia, starts, stops, transparent;
static unsigned gpio_changes, requested_frame_count, rejected_notifications;
static bool bus_enabled, reader_active, try_notification;
static int speaker_mutex;
static FuriMutex* furi_hal_speaker_mutex = &speaker_mutex;
static SignalReader reader;
static SignalReader* furi_hal_nfc_texkom_reader = &reader;
bool furi_hal_speaker_is_mine(void);
void furi_hal_speaker_stop(void);
bool furi_hal_speaker_acquire(uint32_t timeout);
void furi_hal_speaker_release(void);
FuriThreadId furi_thread_get_current_id(void) { return current_thread; }
int furi_mutex_acquire(FuriMutex* m, uint32_t timeout) {
    assert(m == &speaker_mutex && timeout == 0);
    if(mutex_owner) return FuriStatusErrorResource;
    mutex_owner = current_thread;
    return FuriStatusOk;
}
int furi_mutex_release(FuriMutex* m) {
    assert(m == &speaker_mutex && mutex_owner == current_thread);
    mutex_owner = 0;
    return FuriStatusOk;
}
unsigned furi_mutex_get_owner(FuriMutex* m) { assert(m == &speaker_mutex); return mutex_owner; }
void furi_hal_power_insomnia_enter(void) { ++insomnia; }
void furi_hal_power_insomnia_exit(void) { assert(insomnia); --insomnia; }
bool furi_hal_bus_is_enabled(int bus) { assert(bus == FuriHalBusTIM16); return bus_enabled; }
void furi_hal_bus_enable(int bus) { assert(bus == FuriHalBusTIM16 && !bus_enabled); bus_enabled = true; }
void furi_hal_bus_disable(int bus) { assert(bus == FuriHalBusTIM16 && bus_enabled); bus_enabled = false; }
void furi_hal_gpio_init(const int* pin, int mode, int pull, int speed) {
    assert(pin == &gpio_speaker && mode == GpioModeAnalog && pull == GpioPullDown &&
           speed == GpioSpeedLow);
    ++gpio_changes;
}
void furi_hal_gpio_init_ex(const int* pin, int mode, int pull, int speed, int alt) {
    assert(pin == &gpio_speaker && mode == GpioModeAltFunctionPushPull && pull == GpioPullNo &&
           speed == GpioSpeedLow && alt == GpioAltFn14TIM16 && bus_enabled);
    ++gpio_changes;
}
void LL_TIM_DisableAllOutputs(int timer) {
    assert(timer == 16 && bus_enabled && mutex_owner == current_thread);
}
void LL_TIM_DisableCounter(int timer) {
    assert(timer == 16 && bus_enabled && mutex_owner == current_thread);
}
void furi_hal_nfc_transparent_mode_enter(const FuriHalSpiBusHandle* h) {
    assert(h && !transparent); ++transparent;
}
void furi_hal_nfc_transparent_mode_exit(const FuriHalSpiBusHandle* h) {
    assert(h && transparent == 1 && !reader_active); --transparent;
}
void furi_thread_flags_clear(unsigned flags) {
    assert(flags == FuriHalNfcEventInternalTypeTransparentDataReceived);
}
"""

CAPTURE_HARNESS = r"""
static FuriHalNfcTexkomCapture* active_capture;
void furi_hal_nfc_texkom_reader_callback(void* context) { (void)context; assert(false); }
void signal_reader_start(SignalReader* r, void (*callback)(void*), void* context) {
    assert(r == &reader && callback == furi_hal_nfc_texkom_reader_callback && !reader_active);
    furi_hal_bus_enable(FuriHalBusTIM16);
    reader_active = true; ++starts; active_capture = context;
    assert(active_capture->since_impulse == FURI_HAL_NFC_TEXKOM_FRAME_GAP &&
           active_capture->thread_id == current_thread);
}
void signal_reader_stop(SignalReader* r) {
    assert(r == &reader && reader_active);
    reader_active = false; ++stops; active_capture = NULL;
    furi_hal_bus_disable(FuriHalBusTIM16);
}
void furi_hal_nfc_texkom_wait_flags(unsigned flags, unsigned timeout) {
    assert(flags == FuriHalNfcEventInternalTypeTransparentDataReceived &&
           timeout == FURI_HAL_NFC_TEXKOM_RX_TIMEOUT_MS);
    assert(reader_active && active_capture && mutex_owner == current_thread && insomnia == 1);
    if(try_notification) {
        const unsigned owner = current_thread;
        current_thread = 2;
        assert(!furi_hal_speaker_acquire(0)); ++rejected_notifications;
        current_thread = owner;
        assert(mutex_owner == owner && bus_enabled && insomnia == 1);
    }
    assert(requested_frame_count <= sizeof(active_capture->gaps));
    active_capture->frame_count = requested_frame_count;
    for(unsigned i = 0; i < requested_frame_count; ++i) active_capture->gaps[i] = (uint8_t)(10 + i);
}
void assert_released(void) {
    assert(!bus_enabled && !reader_active && !mutex_owner && !insomnia && !transparent &&
           !active_capture && starts == stops);
}
"""


def receiver():
    texkom = (HAL / "furi_hal_nfc_texkom.c").read_text()
    speaker = (HAL / "furi_hal_speaker.c").read_text()
    defines = "\n".join(re.findall(r"(?m)^#define FURI_HAL_NFC_TEXKOM_.*$", texkom))
    capture = re.search(r"typedef struct \{.*?\} FuriHalNfcTexkomCapture;", texkom, re.S).group()
    speaker_functions = "\n".join(function(speaker, signature) for signature in (
        "bool furi_hal_speaker_acquire(", "void furi_hal_speaker_release(",
        "bool furi_hal_speaker_is_mine(", "void furi_hal_speaker_stop(",
    ))
    return (HARNESS + defines + "\n" + capture + "\n" + speaker_functions + CAPTURE_HARNESS +
            function(texkom, "static FuriHalNfcError furi_hal_nfc_texkom_poller_rx("))


class TexkomTimerOwnershipTests(unittest.TestCase):
    def test_busy_timer_skips_window_without_touching_resources(self):
        native(receiver() + r"""
int main(void) {
    FuriHalSpiBusHandle handle = 0;
    uint8_t data[4]; memset(data, 0xa5, sizeof(data)); size_t bits = 123;
    bus_enabled = true;
    assert(furi_hal_nfc_texkom_poller_rx(&handle, data, sizeof(data), &bits) ==
           FuriHalNfcErrorCommunicationTimeout);
    assert(bits == 0 && bus_enabled && !starts && !stops && !transparent &&
           !mutex_owner && !insomnia && !gpio_changes);
    for(unsigned i = 0; i < sizeof(data); ++i) assert(data[i] == 0xa5);
    bus_enabled = false; assert_released(); return 0;
}
""")

    def test_busy_speaker_mutex_skips_even_when_timer_temporarily_disabled(self):
        native(receiver() + r"""
int main(void) {
    FuriHalSpiBusHandle handle = 0; uint8_t data[4] = {0}; size_t bits = 123;
    mutex_owner = 2;
    assert(furi_hal_nfc_texkom_poller_rx(&handle, data, sizeof(data), &bits) ==
           FuriHalNfcErrorCommunicationTimeout);
    assert(bits == 0 && mutex_owner == 2 && !bus_enabled && !starts && !stops &&
           !transparent && !insomnia && !gpio_changes);
    mutex_owner = 0; assert_released(); return 0;
}
""")

    def test_success_timeout_and_overflow_release_every_receive_window(self):
        native(receiver() + r"""
int main(void) {
    FuriHalSpiBusHandle handle = 0;
    const unsigned sizes[] = {0, 4, 8};
    for(unsigned i = 0; i < 60; ++i) {
        uint8_t data[6]; memset(data, 0xa5, sizeof(data)); size_t bits = 123;
        requested_frame_count = sizes[i % 3];
        FuriHalNfcError expected = i % 3 == 0 ? FuriHalNfcErrorCommunicationTimeout :
            (i % 3 == 1 ? FuriHalNfcErrorNone : FuriHalNfcErrorBufferOverflow);
        assert(furi_hal_nfc_texkom_poller_rx(&handle, data, 4, &bits) == expected);
        assert_released();
        assert(bits == (expected == FuriHalNfcErrorNone ? 32U : 0U));
        for(unsigned j = 0; j < sizeof(data); ++j) {
            assert(data[j] == (expected == FuriHalNfcErrorNone && j < 4 ? 10 + j : 0xa5));
        }
    }
    assert(starts == 60 && gpio_changes == 180); return 0;
}
""")

    def test_notification_cannot_take_timer_during_capture_and_can_take_it_after(self):
        native(receiver() + r"""
int main(void) {
    FuriHalSpiBusHandle handle = 0; uint8_t data[4]; size_t bits = 123;
    requested_frame_count = 4; try_notification = true;
    assert(furi_hal_nfc_texkom_poller_rx(&handle, data, sizeof(data), &bits) == FuriHalNfcErrorNone);
    assert(bits == 32 && rejected_notifications == 1); assert_released();
    current_thread = 2;
    assert(furi_hal_speaker_acquire(0)); assert(mutex_owner == 2 && bus_enabled && insomnia == 1);
    furi_hal_speaker_release(); assert_released(); return 0;
}
""")


if __name__ == "__main__":
    unittest.main()
