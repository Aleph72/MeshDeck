# MeshDeck UI

Experimental T-UI fork for the **LilyGO T-Deck Plus, ESP32-S3, 16 MB flash**, based on [jeeab/t-ui-firmware](https://github.com/jeeab/t-ui-firmware) at `f5b334dc50118c6463fc1152902219ec9e2d133e`. Meshtastic radio configuration, channels and identity remain device settings.

The selected fork name is **MeshDeck UI**. Settings places notification volume and its Test button before Back, the final control; the footer below displays the fork name, UI version and Meshtastic version. `FIELD_FORK_NAME` keeps the displayed name in one place. Existing Field update-channel and storage names remain compatible.

## MQTT España and position privacy

The T-Deck Plus can connect directly over its saved Wi-Fi network. The [España connection guide](https://meshtastic.es/docs/mapas/) specifies `mqtt.meshtastic.es`, root `msh/EU_868`, encrypted protobuf packets, JSON and TLS disabled, and client proxy disabled for Wi-Fi. [España's FAQ](https://meshtastic.es/docs/preguntas-frecuentes/) documents server-side channel downlink restrictions. This device's requested configuration also disables downlink locally on every channel, independently of server policy.

The verified device profile enables uplink only on the primary channel and preserves `CLIENT_MUTE`. Its primary-channel position precision and independent MQTT map-report precision are both 14 bits (roughly a 1-2 km approximation); map reports are enabled with location consent and a 3600-second interval. Other channels retain position sharing and MQTT forwarding disabled. No Wi-Fi credentials are compiled into the image. These are saved device settings, not firmware defaults: installing the image on another node does not apply this profile.

The firmware centers coordinates in a truncated grid cell in both paths. Exact GPS remains available locally; map reports still contain altitude as specified by Meshtastic. A real GPS fix and an observed transmitted report are required to validate publication end to end. An absent fix must not be replaced with an invented or exact manually supplied location.

## Included

- A smaller launcher: Mesh, Maps, Calculator, ChatGPT, Ripetitori, Meteo, Settings and Updates. Games, Mail and other optional tiles are removed. Automatic discovery of SD apps is disabled unless `TDECK_FIELD_ALLOW_USER_APPS` is defined; this does not erase their files.
- Eight matching launcher icons: connected Mesh nodes, folded map/pin, calculator, ChatGPT speech bubble, repeater antenna, sun/cloud weather, Settings gear and update arrows. Vector drawing reuses each existing icon canvas without extra child widgets, bitmap buffers or timers. App order, colours, labels, actions and SD-provided icon overrides are preserved.
- Home chat shortcuts for channels and recent direct conversations: tap Preferite, then + or Rimuovi. Up to twelve shortcuts share the launcher grid and show their own unread counts. Opening one clears only that conversation; unavailable chats show a notice. Shortcut identity uses channel/node IDs rather than displayed names.
- Ordina in Home (or long-press any tile): move one position with < > or between pages with Pag - / Pag +. Moves preserve the other tiles' relative order and follow the destination page, including incomplete pages. Chat pins and ordering are saved in a separate local NVS record, so they survive restart without an SD card. Old SD order files seed the layout when needed; failed saves keep the previous layout and show an error. See `qa/field/VALIDATION-home-2026-11-26.md` for actual tests and limits.
- MeshDeck boot splash with the UI version taken directly from the build, preserving boot progress and error messages.
- Meteo: current conditions and five-day forecasts from [Open-Meteo](https://open-meteo.com/en/docs), city search/selection, Celsius/km/h and last successful data cached locally. It uses already-connected Wi-Fi and public city-center coordinates, without reading or sending GPS. Network work is bounded and serialized with the existing TLS jobs. Back closes city search before leaving the app. See `qa/field/VALIDATION-boot-weather-2026-11-26.md` for tests and installation evidence.
- Map downloads correctly handle chunked HTTP responses. Tiles are saved on the SD card for offline use.
- HearHam repeater directory: automatic download when the app opens with Wi-Fi already connected, manual refresh, offline cache, and the nearest 40 entries sorted by GPS distance. Without a fix, the app says so; Zaragoza can be selected explicitly as a reference. The app sends no GPS coordinates to HearHam. Meshtastic location-sharing settings are separate.
- An unofficial ChatGPT client using the official Sign in with ChatGPT flow. Eligible users authorize ChatGPT plan usage in a browser on their Mac once, then the T-Deck uses Wi-Fi directly and renews its session. No API key is needed for this mode, and failures never fall back to API-key billing. This does not expose existing ChatGPT conversations. Only the previous exchange is kept in RAM.
- Adjustable notification volume, initially 80%, restored correctly after alarm sounds. The existing master Sound switch still applies.
- Three original notification melodies (Discreta, Classica, Insistente), plus Originale to preserve the prior sound. Settings saves the choice locally and Test previews it at the current volume. Presets use the Meshtastic message-notification path and its channel/DM mute rules, without the second UI pop. Preview and notifications respect master mute and zero volume; playback is deferred off the UI task and does not interrupt an alarm.
- Signed OTA updates, checked manually or once per day while Wi-Fi is connected and the launcher/lock screen is idle. Automatic checks can be disabled. A newer compatible image is downloaded, checked and installed before a restart.

## Memory diagnostics

The chip icon in Mesh/Home toggles a readout of **free** memory: Heap is internal RAM in bytes and percent of the internal heap; LVGL is free bytes and percent inside its separate 5 MiB PSRAM-backed pool. It is not total free PSRAM. The readout retains its existing ten-second refresh while enabled on the Home panel.

Tap the launcher's clock/title to open Diagnostics. It shows free internal heap, the SDK minimum since boot, the largest currently allocatable internal block, PSRAM free/minimum, LVGL free/largest block/fragmentation, uptime and reset cause. Scroll for additional details and previous-run sampled lows; tap the bottom hint to close. The view is a snapshot, sampled on the UI task. The SDK minimum sums per-region low watermarks; it can be lower than any simultaneous total. Previous-run saved lows are sampled and approximate, not the SDK minimum.

Diagnostics adds no periodic task or flash writes. USB `@@mem` already provides internal free/minimum/largest counters; `@@info` reports uptime, reset cause and LVGL figures. Uptime now uses ESP's 64-bit monotonic timer to avoid the approximately 50-day millisecond wrap. `python qa/field/test-memory-diagnostics.py` exercises the actual readout and bridge functions with sanitizers. See `qa/field/VALIDATION-memory-2026-11-26.md` for checks and remaining hardware validation.

## Connect a ChatGPT account

This is an independent open-source app using [official ChatGPT plan authorization](https://developers.openai.com/siwc/token-sharing-open-source). Availability and usage limits depend on the user's account, workspace and OpenAI policy. Connecting identity alone is insufficient: the user must authorize `chatgpt.tokens.use.direct`.

1. Install the application firmware on the T-Deck Plus. Keep the USB cable connected to the Mac for the initial sign-in.
2. Create an isolated Python 3.10+ environment and install `requirements-siwc.txt`.
3. Run `python bin/connect-chatgpt.py --port /dev/cu.usbmodem2201 --state-dir /private/path/tdeck-field`. Choose a directory outside the repository, preferably on your external SSD.
4. Open the printed authorization URL in a browser **on that Mac**. The callback listens only on `127.0.0.1`; opening it on a phone will not return to the Mac.
5. Authorize the app's ChatGPT plan usage. The helper verifies state, PKCE, the signed ID token, issuer, audience, expiry, nonce and granted scopes. It reads the account's current model catalog and selects its first visible model.
6. The helper transfers the session over USB in bounded chunks with a SHA-256 integrity check. The firmware validates its device host ID, persists it and confirms the import. After this, inference and token renewal work over the T-Deck's Wi-Fi; the Mac is not an inference proxy.

The helper keeps account/client registrations separate with `--profile NAME` or `--new-account`. Registration metadata has owner-only permissions. An interrupted transfer leaves a protected `pending-session.json`; retry with `--resume` for the same profile. Successful transfer removes that pending credential file. Never put the state directory in Git, a shared folder or the SD map directory.

Device credentials are in application NVS, outside exported Meshtastic configuration and outside the SD card. This does not add flash encryption or resistance to physical extraction; treat full flash backups as secrets. Session refreshes replace the access token, expiry, scopes and rotating refresh token together. After a long period without renewal, a new browser sign-in may be needed. Sign out attempts remote revocation and reports when only local disconnection is confirmed.

The account UI displays the connected account, selected model and a link to ChatGPT usage settings. Plus allowances remain subject to shared plan and app limits. Existing legacy API keys are not read by this client and are never used as a fallback.

Responses use `store:false`, `stream:true` and explicit conversation input. Output is bounded and UTF-8 safe; the app waits for `response.completed`, distinguishes interrupted and failed responses, and surfaces plan-usage errors. It does not send unsupported token-budget fields or use private ChatGPT backend endpoints.

Host checks: `python qa/field/test-connect-chatgpt.py` tests the callback with local simulated authorization, including bad state, PKCE, signatures, nonce, expiry, private files and USB transfer integrity. The C++ plan parser is exercised by `bin/test-field-host.py` with sanitizers. Real account authorization and device inference must also be tested before treating the integration as validated.

## OTA trust and recovery

The update channel is `Aleph72/MeshDeck`, branch `field`, file `ota/stable.json`. It contains a base64 payload and an ECDSA P-256/SHA-256 signature together in one document. The firmware verifies the signature using `src/FieldUpdateKey.h` before trusting the manifest.

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

Notification melody checks: `python qa/field/test-notification-tones.py` exercises the actual deferred selection/playback service with host stubs and sanitizers, including saved preferences, invalid values, mute, volume zero, busy playback and alarm priority. It also validates the embedded WAV files. USB diagnostics `@@tonestatus`, `@@tone 0..3`, `@@tonetest` and `@@notifytest` support device checks; the last simulates an incoming alert locally without radio transmission.
