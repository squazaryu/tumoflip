# KeeLoq Keystore Decryptor for Flipper Zero
As the name suggests, if you've made it this far, you already know what the purpose of all this is.\
I'm not gonna share decrypted database here, at least build it by yourself.\
Decrypted keys will be stored on your SD Card under `/ext/keystore_decrypted.txt`.

## Tumoflip integration
This is packaged as an isolated `ARF Tools` external app. It is imported from
source only; prebuilt artifacts are not shipped.

The output file can contain sensitive KeeLoq material from the installed
firmware keystore. Keep `/ext/keystore_decrypted.txt` on your own SD card and
do not commit or share it.

## Why
To find out what types of keys are bundled with the firmware.

## Features
Decrypt KeeLoq database bundled with the firmware.
Works with any currently released firmware (e.g. Momentum rev. MNTM-012).

## Screenshots
|||
|:---:|:---:|
| ![app-list](docs/images/01-app-on-list.png) | ![decrypted-keys](docs/images/02-decrypted-info.png) |

## Notes & limitations
- Decryption must be performed on the Flipper Zero device because the AES key is stored in its secure enclave.
- If any firmware owner uses a key from a different slot, the enclave ID will need to be adjusted. The same applies to IV permutations.
