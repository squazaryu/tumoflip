# Frequency Analyzer notification ownership

Adapted from Unleashed 7f76ec441f5a7f74dfc061c5575200f20ea4c5c8 (#1131).

Standard Sub-GHz and ARF both open the notification record in the parent app
and close it when the app is freed. The analyzer view used to close that same
record on every exit despite never opening it. The view now only stops and
frees its worker. The parent remains responsible for its notification handle.

The host regression executes the production exit function for both copies,
100 times each, alternating running/stopped workers. It checks worker cleanup
and preservation of the parent's notification record until parent teardown.

The upstream analyzer plugin extraction is not included. Tumoflip has additional
preset scanning, frequency notebook and receiver transitions that require a
separate plugin interface and packaging adaptation. Existing analyzer behavior
and SDK API 88.5 are retained. The previously declined Loader #1129 is not included.

## Physical acceptance (pending)

- Open and close Frequency Analyzer repeatedly in Standard and ARF.
- Check sound, vibration and LEDs after returning to the parent app.
- Check frequency notebook saving, preset scan and transition to receiver.
- Exit Sub-GHz, reopen it, and repeat. No crash or lost feedback is expected.
