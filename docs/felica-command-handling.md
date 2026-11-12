# FeliCa command-handling adaptation

Source: official firmware
[`3fb1d517`](https://github.com/flipperdevices/flipperzero-firmware/commit/3fb1d51727733d2007abc5ec636a89328857803e)
([Fix FeliCa command handling](https://github.com/flipperdevices/flipperzero-firmware/pull/4465)).

## Behavior

- Re-arm reception after rejected, malformed, wrong-CRC, wrong-IDm and
  unsupported commands instead of leaving emulation waiting indefinitely.
- Standard emulation answers Polling only for saved systems. Lite-S/NDEF
  behavior remains available for Lite; Standard hardware Polling reports the
  first saved system code.
- Keep widened emulation response timings in a listener-local PMm copy.
  The saved dump's PMm is preserved.
- Calculate read/write/service command timeouts from the card's PMm and the
  operation's unit count, retaining the existing minimum Polling timeout.
- Parse the full 16-bit extended block address. The existing dump stores
  8-bit block indices; an unsaved high address is rejected without aliasing
  its low byte to a different block.
- Validate all write targets before modifying any block. Retain Tumoflip's
  exact frame-length, block-list and complete-payload checks.
- Select a saved system by its addressed IDm while preserving the base IDm.

The optional system-key-version query remains a late, nonfatal pass. File
format version stays 4, with legacy version-2/3 reads retained.

## Compatibility and validation

The new `FelicaErrorIgnore` value is appended to the public enum, preserving
all old values. Local F7 API advances from 88.18 to **88.19**. No public method
or variable export is added or removed; this is not an import of the official
API's numeric version or unrelated GPS/MJS exports.

Native host regressions compile production code with ASan/UBSan and synthetic
frames/dumps. They cover rejected-frame recovery, direct system addressing,
Polling system restrictions, PMm preservation, atomic writes, truncated frames,
extended-address non-aliasing and all 256 PMm parameter values across command
classes and boundary unit counts. A bounded response buffer avoids per-read
heap allocation in Standard emulation.

Hardware acceptance remains pending. After the Dev installation, check your
own FeliCa Standard and Lite-S dumps/cards: repeated emulation after a rejected
request, complete reading, unchanged saved PMm, expected system codes, and
normal NFC save/reopen behavior. Host and upstream tests do not establish
acceptance on your particular reader/card generation.
