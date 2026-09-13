# External radio disconnects and FAP asset progress

Adapted from Unleashed commits:

- 8009e8135088c6d90711ff4daaed921699627c0c (PR 1139)
- 69c036819ef2b8415ac3c579104b039e43f91ec6

## Radio

Standard and ARF check the selected external radio before starting RX/TX and
during receiver, receiver-info and Read RAW ticks. Loss during asynchronous RX
stops the workers and peripherals before selecting internal and restarting RX.
Standard Auto/diversity retains its requested mode across fallback. The idle
Sub-GHz menu can restore the requested external mode after five seconds.
Explicit Internal selection disables automatic reacquisition.

The existing radio broker retains ownership of external power. A new selection
tears down the old driver and uses the result of begin's hardware self-test.
Failed begin is paired with end. An unplugged external radio no longer triggers
a fatal IDLE/RX/TX state check. TX returns failure and leaves the E07 amplifier
off if the chip never enters TX. The internal driver's invariant checks remain.

The bounded retune timeout and PARTNUM/VERSION check already in Tumoflip remain.
RAW fallback preserves samples buffered for SD: receiver reset would discard
them. Decoded reception resets partial decoders before resuming.

No automatic radio switch occurs during TX. A transmission interrupted by
unplugging is not retransmitted automatically. Synchronous analyzer ownership
is excluded from asynchronous receiver recovery. Other driver users receive
nonfatal state handling; their app-specific recovery is outside this change.

## Asset extraction

The loader shows an animated hourglass and a progress bar only when an FAP
actually extracts bundled files to SD. A matching cached asset signature does
not show the overlay. Progress counts completed files, not bytes or application
initialization. Signature write and sync must succeed before reporting completion.

The preload call is synchronous on the loader thread. Its callback is cleared
and its overlay detached on return, before either the preload error dialog or
the application thread starts. No callback survives to an app's Back/exit path.
The broader upstream loading-depth/first-viewport mechanism is not required.

The three helper functions are internal (not exported to FAPs); public API
remains 88.6 and existing FW Packages stay compatible.

Compact non-debug builds compile the two changed asset/lifecycle translation
units with -Os instead of the library's historical -Og. The ELF relocation
engine and debug builds retain their flags. This reduces flash use without
removing features or weakening the 4 KiB erase-page safety reserve.

## Validation

Host fixtures execute the production radio polling and asset-overlay functions
for disconnect, Auto preference, RAW buffer preservation, cooldown/tick wrap,
explicit Internal, synchronous RX/TX exclusion, completion, failure and reentry.
The render script uses production draw calls and the actual hourglass frames.

Hardware acceptance after installing the release:

1. Standard and ARF: attach external CC1101, open Read, unplug, confirm fallback
   and responsive navigation. Repeat with hopping and during Read RAW recording.
   Save/reopen RAW and confirm samples before disconnect remain.
2. Standard Auto: repeat disconnect, return to the menu after five seconds,
   reconnect and confirm the requested mode returns. Explicit Internal must stay
   internal. Confirm independent Standard/RAW frequency and modulation settings.
3. Unplug before TX and during TX of a test signal; stop/Back must work. Do not
   treat an interrupted transmission as delivered. Check E07 amplifier shutdown.
4. Launch an FAP with uncached bundled assets, then reopen it. Check progress on
   the first extraction, no stale bar on Back or subsequent cached launch, and
   a clean error path for a failed extraction (use a disposable SD test copy).

Host checks and artifact validation do not establish these hardware results.
