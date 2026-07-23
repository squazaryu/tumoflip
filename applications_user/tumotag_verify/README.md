# TumoTag Verify

TumoTag Verify is an on-device, read-only verifier for saved NFC, LF RFID, and
iButton artifacts.

## Workflows

- **Verify saved artifact** compares a selected file with a physical token.
- **Verify after write** performs the same read-back comparison after a write
  completed in the stock application.
- **Find saved token** scans the matching storage tree and reports the best
  deterministic match.
- **Last result** opens the most recent report.

`Verified` requires matching protocol and readable payload. `Partial` means the
available data matches, but protected or unreadable regions were not proven.
The app never writes to a token, guesses keys, or modifies source artifacts.

Reports are saved under `/ext/apps_data/tumotag_verify/reports`.
