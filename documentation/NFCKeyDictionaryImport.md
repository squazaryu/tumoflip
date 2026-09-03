# MIFARE Classic key dictionary import

Adapted from Unleashed `95c35fb82de2390531a1788b28612a793cd987ae`.

## User flow

Open a read or saved MIFARE Classic card, then choose **Save Keys to Dictionary** next to
**Show Keys**. The action is present only when at least one Key A or Key B is marked as found.
The result reports new keys and keys already known by either dictionary.

The collector uses the found masks, not zero/nonzero key bytes. A recovered all-zero key is
therefore retained, while an unrecovered zero-filled slot is ignored. Repeated sector keys
are collected only once. The same flow works regardless of whether the keys came from a CUID
dictionary, a recovery operation, manual input, or a loaded dump.

## Storage contract

- System and user dictionary scans are read-only, accept lowercase hex, and check read errors.
- Missing, unreadable, or empty system dictionaries stop the import before user-file writes.
- The existing user dictionary is backed up beside itself as `.nfc_key_import*.bak`; the copy
  is synchronized and its size checked before an append can begin.
- Only new keys are appended. A missing final newline is added only as part of a successful
  transaction, never while scanning.
- Success is reported only after the complete write, explicit storage sync, and close succeed.
- On failure, the file is truncated back to its original size and synchronized. If that fails,
  the backup is copied back and verified before it can be removed.
- If storage also prevents recovery, the backup is retained and the result says recovery is
  required. Do not delete that backup; copy it off the SD card before attempting manual repair.
- An interrupted operation cannot restore a physically removed or unavailable SD card. The
  retained backup is the recovery source in that case; hardware acceptance remains separate.

The shared `lib/toolbox/keys_dict` implementation, NFC dump files, and per-UID key caches are
unchanged. This adaptation is contained in the NFC FAP and its MIFARE Classic plugin; it adds
no firmware API symbol.

## Automated checks

```sh
python3 -m unittest tools.tumoflip.test_nfc_key_dictionary_import \
  tools.tumoflip.test_keys_dict_heap \
  tools.tumoflip.test_nfc_mifare_classic_cuid_iteration
./fbt -j2 COMPACT=1 DEBUG=0 fap_nfc
```

The host fixture compiles the production importer and injects storage failures. It covers
duplicate filtering, lowercase input, newline preservation, 80-key capacity, missing/empty
system dictionaries, read/open/close errors, partial writes, sync failures, backup failures,
truncate rollback, backup restoration, and backup retention after a double failure.

## Hardware acceptance (not yet completed)

1. Back up `nfc/assets/mf_classic_dict_user.nfc` and use a card/dump you own.
2. Import a card with one new key plus system/user duplicates. Verify the counts and that all
   previous dictionary bytes and keys remain intact.
3. Repeat the import: it must report zero new keys and leave the dictionary unchanged.
4. Import a partially read card: only found A/B slots may be exported.
5. Reopen NFC and perform an ordinary read using the newly saved key; also test a CUID card.
6. Reboot and confirm the dictionary persists. Recheck NFC read/save/reopen/emulation.
7. Exercise missing-system-dictionary and storage-error states on a disposable test SD copy,
   never on the only copy of a personal dictionary. Confirm the error is visible and recovery
   data is not discarded.
