#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const void* miso;
} FuriHalSpiBusHandle;

bool furi_hal_gpio_read(const void* pin);

bool furi_hal_spi_bus_trx(
    const FuriHalSpiBusHandle* handle,
    const uint8_t* tx,
    uint8_t* rx,
    size_t size,
    uint32_t timeout_ms);
