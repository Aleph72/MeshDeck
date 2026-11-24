# T-Deck Field validation - 2026-11-24

Hardware: LilyGO T-Deck Plus, ESP32-S3, 16 MB flash, octal PSRAM. Firmware Field 2026.11.24.3, based on upstream f5b334d. This is the only tested hardware target.

- PlatformIO builds passed with pioarduino 55.03.39 / Arduino 3.3.9. The octal-PSRAM check passed.
- C++ host checks passed under AddressSanitizer and UndefinedBehaviorSanitizer: bounded OpenAI text extraction; streaming HearHam import (24,230 valid of 24,400 records); OTA manifest validation; ChatGPT-plan credential bounds and device host binding; scope boundaries; streamed UTF-8; overflow; terminal completion, interruption and usage-limit handling; numeric and ISO token timestamps.
- Six Python OAuth-helper tests passed: signature/issuer/audience/expiry/nonce; timestamp normalization; owner-only atomic files; transfer length and SHA-256; fragmented USB replies; local callback rejection of bad state and PKCE/code exchange.
- A deliberately corrupted credential transfer was rejected by the actual device without activating an account.
- A real account completed the official OAuth flow. The helper verified identity and the permission to use the ChatGPT plan, discovered a model with the OAuth token, transferred the session and received confirmation that it was saved. No API key was used for this mode.
- The actual T-Deck sent the controlled prompt `Rispondi solo OK.` over Wi-Fi and displayed `OK` after `response.completed`. There was no desktop inference proxy and no fallback to API-key billing.
- The account remained configured after a software reboot. Node identity, public key, radio/configuration sections, module settings and channels matched their immediate pre-update/pre-reboot snapshots.
- Application partitions only were updated; map and filesystem partitions were not written. A private recovery backup was kept outside the source repository. Credentials and account identifiers are excluded from these validation notes and release artifacts.

Limitations: an actual one-hour token-expiry/refresh cycle and 30-day inactivity were not waited out or forced. Those paths are implemented but remain provider-integration checks. Complete signed-OTA installation still awaits publication/authentication of the GitHub release channel. Bootloader rollback capability has not been established. ChatGPT plan eligibility, available models and limits remain account/workspace policy decisions.
