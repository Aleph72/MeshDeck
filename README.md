# MeshDeck

MeshDeck is firmware for running **Meshtastic directly on the LILYGO T-Deck Plus**, with an interface designed for its screen, keyboard, and trackball. It builds on [T-UI](https://github.com/jeeab/t-ui-firmware) and can exchange messages with [Meshtastic](https://meshtastic.org/) nodes configured for the same mesh.

## Features

- **Customizable home screen** with pinned conversations, unread counts, and apps you can move between pages.
- **Offline maps** with tiles stored on microSD, a HearHam repeater directory, and a calculator.
- **Weather** with city search, five-day forecasts, and cached results.
- **Settings and diagnostics** with notification tones, volume control, keyboard lock, and memory statistics.
- **English, Italian, and Spanish** for the home screen, favorites, app arrangement, main settings, and weather. Other screens are still being translated.

## Hardware and installation

MeshDeck has been tested on the **LILYGO T-Deck Plus with LoRa, ESP32-S3, and 16 MB of flash**. The original T-Deck still needs validation. The T-Deck Pro has an e-ink display and requires a dedicated port.

Version **2026.11.26.6** was tested on the T-Deck Plus. The browser installer and public update channel are in development. A first installation and an update to an existing MeshDeck installation use separate procedures.

## Building and contributing

The build uses Python 3.10+ and PlatformIO with the `t-deck-tft` target. See the [technical documentation](README.field.md) for build steps, dependencies, and tests.

Bug reports and contributions are welcome. When reporting an issue, include your device model, MeshDeck version, and steps to reproduce it.

## License and credits

MeshDeck is an independent project based on T-UI and Meshtastic. The [original T-UI README](README.upstream.md) preserves the upstream credits. MeshDeck is distributed under the [GPL-3.0 license](LICENSE).
