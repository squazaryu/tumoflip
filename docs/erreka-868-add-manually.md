# Erreka 868 MHz Add Manually adaptation

## Behavior

Standard Sub-GHz -> Add Manually lists `Erreka 868MHz` immediately after
`Erreka 433MHz`. Both use the existing Erreka KeeLoq generator and AM650;
the new profile selects **868350000 Hz**, while the existing profile keeps
**433920000 Hz**. Default serial and seed use the existing 20-bit mask;
button and counter remain `0x02`. Advanced mode uses the existing serial,
button, counter and seed input screens.

Saving a loaded Erreka file preserves its `Seed` field, including zero.
Generation reports failure when writing `Seed` or `Manufacture` fails.
The existing BFT and Monarch saved-field behavior is retained.

## Upstream and compatibility

- Frequency/profile addition: Unleashed
  [`8a7f3f44d3`](https://github.com/DarkFlippers/unleashed-firmware/commit/8a7f3f44d39ac258cef2a42935c1de8183505880).
- Erreka seed persistence: the serialization subset of Unleashed
  [`1139732616`](https://github.com/DarkFlippers/unleashed-firmware/commit/1139732616045a15b9efc59790cc83e3a8ad292a).
- This adaptation does not add the adjacent programming-mode action or
  seed-recovery app. Existing transmit-policy checks remain in place.
- `SetTypeErreka868` is appended to the selection enum; existing numeric IDs
  and the generation structure are unchanged. Public API remains **88.18**
  and the compatible feature-plugin ABI remains **1003**.
- The next firmware updater must include its rebuilt Sub-GHz Add Manually
  plugin. No iOS change or separate custom FW Packages FAP update is required.

## Verification

The host regression executes the production C generator, menu population,
seeded-generation wrapper and KeeLoq saved-field serializer/deserializer.
It checks both frequencies, 20-bit serial/seed boundaries, existing menu IDs,
adjacent entries, Seed round-trips, and failed metadata writes.

Hardware acceptance remains pending. On an owned compatible remote/receiver:

- Check both Erreka entries in Standard Sub-GHz -> Add Manually.
- Create a test file with the 868 MHz profile and inspect its frequency,
  modulation, manufacturer and Seed fields.
- Use Advanced mode with known test values, save, reopen and save a copy;
  confirm Seed and the original file are preserved.
- Repeat creation with the 433 MHz profile.
- Test the intended receiver interaction separately; host tests do not prove
  receiver compatibility.
