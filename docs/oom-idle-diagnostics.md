# Idle OOM diagnostics

This Dev change improves evidence for a future allocation failure. It does not
establish the cause of the previously reported idle crash or certify week-long
hardware stability.

## What changes

- `BtSrv` receives a 2 KiB stack instead of 1 KiB. The inspected device had only
  80 unused bytes at its prior high-water mark; this is a margin improvement,
  not proof that its heap failure was a Bluetooth stack overflow.
- A failed nonzero allocation preserves the **original requested byte count**,
  including requests that overflow allocator alignment/overhead arithmetic.
  Genuine `malloc(0)` remains a separate programming error.
- Before verbose crash logging, persist the kind, firmware commit, sequence,
  active-app label, faulting-thread prefix, requested bytes, free heap, largest
  block and uptime. No allocator, SD writes, user captures or raw app arguments
  are used by the snapshot/RTC writer.
- The optional OOM record fits STM32WB's **20 physical backup registers**.
  System/PIN registers 0..7 stay untouched; legacy crash indices 8..14 remain
  readable. Active-app context moves to volatile C1 RAM, allowing OOM snapshots
  to occupy 8..19 without being overwritten by normal startup/app transitions.
- The marker is written last. A CRC/checksum mismatch or interrupted write is
  rejected, not displayed as a partly valid context. Acknowledgement invalidates
  the marker; closing the normal crash popup does not acknowledge this journal.
- Common private journal code is shared rather than duplicated by inlining/LTO.
  No public firmware export or private Sub-GHz ABI is changed by diagnostics.

## Read after a crash

Use USB CLI `tumocrash show` **before** clearing the report in Companion or with
`tumocrash clear`. Reinstalling firmware first is not necessary and can lose
useful context. Dismissing the reboot popup is safe for this journal.

The existing schema-1 wire vocabulary remains compatible with current Companion:

```text
CRASH schema=1;status=ok;kind=other;app=unknown;seq=2;build=ABCDEF12;reason=oom;thread=BtSrv;req=65536;free=62000;largest=61000;uptime_s=430000
```

`kind=other` is the legacy-client category; **`reason=oom`** is the dedicated new
cause. Current iOS can display the basic report without an update, but does not
yet render the added numeric fields. Those are available via USB CLI. No new
iOS IPA is required for firmware/package installation.

- `thread`: up to 8 safe ASCII bytes; `app`: the existing 12-byte owner label.
  Prefixes may be truncated. User-defined labels can still be private: review
  diagnostics before posting; do not automatically publish raw device logs.
- `req`: full 32-bit original allocation size, before allocator overhead.
- `free` / `largest`: exact values in the physical C1 heap range. The compact
  18-bit encoding reserves all-ones for unavailable/out-of-range data, reported
  as `4294967295`; that value is **not** 4 GiB of available RAM.
- `uptime_s`: seconds since the current RTOS tick epoch, with its normal tick
  rollover (about 49.7 days at 1 kHz). The stored field saturates above 28 bits.
- No optional details are invented for old records or an uncaptured failure.
  Old `kind=other` records cannot be retrospectively identified as OOM.

The maximum complete reply fits the existing 209-byte FAB2 payload budget.
Short internal buffers never receive half an optional-details group.

## Verification and device acceptance

Native tests use the actual journal/formatter/allocator-hook code with fake RTC,
kernel and serial-free interfaces under ASan/UBSan. They cover legacy records,
20-register bounds, preserved system/PIN words, every interrupted write position,
corruption, packed-count boundaries, thread sanitization, app transitions,
pre-scheduler failures and complete schema-1 replies across buffer capacities.
The normal and portable name encodings match the old little-endian register bytes.

After installing the new Dev on owned hardware:

1. Confirm firmware version/API, then check BLE, Companion and screen streaming.
2. Check `top 0`: BtSrv's usable stack should now be approximately 2044 bytes.
3. Leave the normal desktop idle and observe the user's usual connection pattern.
   Short native/CLI tests are not a substitute for the previously failing duration.
4. If another crash occurs, preserve `tumocrash show` and the exact popup text.
   Do not deliberately exhaust RAM or interrupt SD writes on the user's device.

The full 4096-byte erase-aligned flash reserve remains a release gate. Increasing
flash headroom does not fix RAM exhaustion. Stable promotion and physical-device
acceptance remain separate from this diagnostic Dev publication.
