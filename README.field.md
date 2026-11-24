# T-Deck Field

Experimental T-UI fork for the **LilyGO T-Deck Plus, ESP32-S3, 16 MB flash**, based on [jeeab/t-ui-firmware](https://github.com/jeeab/t-ui-firmware) at `f5b334dc50118c6463fc1152902219ec9e2d133e`. Meshtastic radio configuration, channels and identity remain device settings.

## Included

- A smaller launcher: Mesh, Maps, Calculator, ChatGPT, Ripetitori, Settings and Updates. Games, Mail and other optional tiles are removed. Automatic discovery of SD apps is disabled unless `TDECK_FIELD_ALLOW_USER_APPS` is defined; this does not erase their files.
- Map downloads correctly handle chunked HTTP responses. Tiles are saved on the SD card for offline use.
- HearHam repeater directory: automatic download when the app opens with Wi-Fi already connected, manual refresh, offline cache, and the nearest 40 entries sorted by GPS distance. Without a fix, the app says so; Zaragoza can be selected explicitly as a reference. The app sends no GPS coordinates to HearHam. Meshtastic location-sharing settings are separate.
- An unofficial OpenAI API client, with a masked key-entry screen, configurable model, bounded responses and the previous exchange kept only in RAM. It requires Internet and a personal API key; it does not run ChatGPT locally. `store:false` is requested. Real authenticated completions have not yet been tested in this development session.
- Adjustable notification volume, initially 80%, restored correctly after alarm sounds. The existing master Sound switch still applies.
- Signed OTA updates, checked manually or once per day while Wi-Fi is connected and the launcher/lock screen is idle. Automatic checks can be disabled. A newer compatible image is downloaded, checked and installed before a restart.

## OTA trust and recovery

The update channel is `Aleph72/t-ui-firmware`, branch `field`, file `ota/stable.json`. It contains a base64 payload and an ECDSA P-256/SHA-256 signature together in one document. The firmware verifies the signature using `src/FieldUpdateKey.h` before trusting the manifest.

The signed manifest fixes the board, chip, application-image type, version, URL, byte count and SHA-256. Only this repository's release URLs are accepted. Downloads go to the inactive app partition; incomplete, oversized, wrong-format or hash-mismatched images are aborted. Boot selection changes only after image validation. Bootloader, filesystem, keys and configuration partitions are not part of the update.

If the installed bootloader supports rollback, a pending image is confirmed after one minute of normal main-loop operation. Automatic rollback is **not guaranteed** for bootloaders built without that feature. Keep the original USB recovery method available.

Release signing keys are not in this repository and must never be uploaded. The channel remains unavailable until its signed manifest and release assets are published. Fork maintainers must replace both the verification key and channel URLs with their own.

## Build

Use Python 3.10+ and `python bin/build-field.py`. The helper keeps PlatformIO, its cache and temporary files in `.field-build/` beneath this checkout. Cloning onto an external SSD keeps the toolchain there too. The tested platform is pioarduino `55.03.39` / Arduino `3.3.9`; the environment is `t-deck-tft`.

The helper applies the known project-local PlatformIO/SCons workaround and prepares missing Arduino dummy build files. The required mDNS and LittleFS component versions are pinned. A fresh-machine run of the helper remains to be tested; the firmware itself was compiled using these versions on macOS ARM64.

Host parser checks use Clang AddressSanitizer and UndefinedBehaviorSanitizer: `python bin/test-field-host.py --hearham /path/to/public-hearham.json`. Download the public snapshot from `https://hearham.com/api/repeaters/v1`; it is not bundled. Set `PLATFORMIO_CORE_DIR` if using an existing toolchain outside `.field-build/core`. The checks compile the actual repeater and manifest parsers and test bounded OpenAI text extraction.

Flash an **application** image only, using a suitable update procedure. Never use a factory/merged image for OTA. Initial USB setup and an appropriate 16 MB partition layout are required before computer-free OTA can work.

## Publish a signed release

1. Update `FIELD_APP_VERSION` and `kFieldVersionCode` in `util/FieldUpdate.h`, then build and test.
2. Run `python bin/sign-field-release.py APP.bin --key /private/location/private.pem --tag v0.1.0 --output /new/release-directory`.
3. Publish `firmware-t-deck-plus.bin` and the generated audit files as assets of that GitHub release.
4. Commit the generated `stable.json` to `ota/stable.json` on branch `field` **after** the asset is available.

The signing helper refuses merged/factory images, mismatched versions, the wrong signing key, and output directories that already exist. Signed files are public; the private key and full device backups are not.

## Data and credits

Repeater data is downloaded directly from [HearHam](https://hearham.com/repeaters), whose site explicitly permits use in online/offline applications. No repeater database is bundled in the repository or firmware. Incomplete records and unsupported frequencies are excluded; positions and operational flags are community data. The initial importer test accepted 24,230 of 24,400 records.

Map attribution remains visible for the selected provider, including BKG TopPlusOpen. Original Meshtastic and T-UI credits and the **GPL-3.0** license are retained. This is an independent fork, not an official OpenAI, HearHam, Meshtastic or LilyGO product.

The T-Deck Plus is the only tested hardware target. See the original README below and the upstream repository for the broader firmware project.
