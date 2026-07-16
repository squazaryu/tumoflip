#include <cc1101.h>
#include <furi_hal_cortex.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint8_t captured_tx[16];
static size_t captured_size;

bool furi_hal_gpio_read(const void* pin) {
    (void)pin;
    return false;
}

FuriHalCortexTimer furi_hal_cortex_timer_get(uint32_t timeout_us) {
    return timeout_us;
}

bool furi_hal_cortex_timer_is_expired(FuriHalCortexTimer timer) {
    (void)timer;
    return false;
}

bool furi_hal_spi_bus_trx(
    const FuriHalSpiBusHandle* handle,
    const uint8_t* tx,
    uint8_t* rx,
    size_t size,
    uint32_t timeout_ms) {
    (void)handle;
    (void)timeout_ms;

    if(size > sizeof(captured_tx)) return false;
    captured_size = size;
    if(tx) memcpy(captured_tx, tx, size);
    // The host ABI may store CC1101Status bitfields wider than the target's single byte.
    if(rx) memset(rx, 0, size * sizeof(CC1101Status));
    return true;
}

int main(void) {
    const FuriHalSpiBusHandle handle = {0};
    const uint32_t actual_frequency = cc1101_set_intermediate_frequency(&handle, 152344U);

    if(captured_size != 2U) {
        fprintf(stderr, "expected one register write, got %zu bytes\n", captured_size);
        return 1;
    }
    if(captured_tx[0] != CC1101_FSCTRL1) {
        fprintf(
            stderr,
            "expected FSCTRL1 (0x%02X), got register 0x%02X\n",
            CC1101_FSCTRL1,
            captured_tx[0]);
        return 1;
    }
    if(captured_tx[1] != 6U) {
        fprintf(stderr, "expected IF word 6, got %u\n", captured_tx[1]);
        return 1;
    }
    if(actual_frequency != 152343U) {
        fprintf(stderr, "expected synthesized IF 152343 Hz, got %lu\n", (unsigned long)actual_frequency);
        return 1;
    }

    return 0;
}
