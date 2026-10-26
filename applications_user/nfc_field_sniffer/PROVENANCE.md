# Passive NFC field sniffer

- GPL-3.0 source boundary: D4C1-Labs/Flipper-ARF
  `9b18419de2774a9693f91a3631653d1538f0f91a`,
  `applications/system/nfc_sniffer/nfc_sniffer_hf.c`.
- ARF credits @thedoctorz/doctor-firmware for the original passive sampler and
  demodulation. Both published repository root licenses are GPL-3.0.
- Only passive ST25 configuration, PB4 sampling and bounded Miller/Manchester
  demodulation are adapted. Key-recovery Python tools, relay/active reader flows,
  original UI/trace writer and large runtime allocation scheme are not imported.
- This document records provenance, not author endorsement, RF certification or
  completion of any protected-source acceptance ledger.

## Tumoflip changes

- Ring8192 plus a2048-byte staging copy, static bounded decoder workspace;
  no65,500-byte runtime allocation or highest-priority capture thread.
- Reserve NFC, DMA1-channel1 IRQ and TIM2 IRQ/clock with failure rollback. Never
  reset a busy timer or overwrite an existing ISR. Requires Tumoflip's new
  `furi_hal_interrupt_try_set_isr` export (API88.23); not upstream API parity.
- Check DMA progress before/after copying, flag discontinuities after excessive
  scheduling latency, bound the capture to2 seconds and restore GPIO, timer,
  channel, IRQ ownership and low-power NFC state on completion/cancel/error.
  Shared DMA clocks are not disabled, because other channels may use them.
- Accept a reader's already-present external field; do not require the owner to
  switch it off/on. Explicit field detection is re-enabled after chip reset.
  RF TX remains off and no transmit command is issued.
- Keep short zero-valued card candidates, avoid aliasing sample-word reads,
  report lost regions/frames/truncation, and label CRC unverified unless matched.
  CRC matches are integrity observations, not authentication.
- Native Flipper menus, explicit worker join on Back, and manual create-new trace
  save with sync/close/readback. No background service, automatic logging or
  modification of existing NFC/Sub-GHz captures. Trace files may be private.

## Verification boundary

Synthetic REQA7/CRC-A/noise tests, real-constant register-configuration tests,
resource/failure/Back/save fixtures and F7 FAP/import checks are development
evidence only. Near-field positioning, card replies, concurrent BLE behavior,
post-exit native NFC operation and real overrun handling need owner hardware
acceptance. This package remains experimental; no connected hardware was used.
