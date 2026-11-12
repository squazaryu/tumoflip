#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef enum {
    NfcFieldCaptureComplete,
    NfcFieldCaptureNoField,
    NfcFieldCaptureCancelled,
    NfcFieldCaptureBusy,
    NfcFieldCaptureError,
} NfcFieldCaptureResult;
typedef bool (*NfcFieldStop)(void* context);
NfcFieldCaptureResult nfc_field_capture(uint32_t timeout_ms, NfcFieldStop stop, void* context);
