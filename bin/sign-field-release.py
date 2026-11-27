#!/usr/bin/env python3
"""Create a signed, public OTA release. Never copies or prints the private key."""

import argparse
import base64
import hashlib
import json
import pathlib
import re
import struct
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument("image", type=pathlib.Path)
p.add_argument("--key", type=pathlib.Path, required=True)
p.add_argument("--tag", required=True)
p.add_argument("--output", type=pathlib.Path, required=True)
a = p.parse_args()
if not re.fullmatch(r"v[0-9]+(?:\.[0-9]+){1,3}", a.tag):
    p.error("Use a numeric release tag, for example v0.1.0")
if ROOT == a.key.resolve() or ROOT in a.key.resolve().parents:
    p.error("Keep the signing key outside the source repository")
if a.output.exists():
    p.error("Output directory already exists; choose a new directory")
header = (ROOT / "lib/meshtastic-device-ui/include/util/FieldUpdate.h").read_text()
version = re.search(r'#define FIELD_APP_VERSION "([0-9.]+)"', header).group(1)
code = int(re.search(r"kFieldVersionCode\s*=\s*([0-9]+)u", header).group(1))
year, month, day, sequence = map(int, version.split("."))
if not 0 < sequence < 100 or code != (
    (year * 10000 + month * 100 + day) * 100 + sequence
):
    p.error("Version text and code do not agree")
image = a.image.read_bytes()
if any(x in a.image.name.lower() for x in ("factory", "littlefs", "merged")):
    p.error("OTA accepts an application image, never a merged/factory/filesystem image")
if (
    not 1024 <= len(image) <= 0x640000
    or image[0] != 0xE9
    or struct.unpack_from("<I", image, 32)[0] != 0xABCD5432
):
    p.error("Invalid ESP application image or size")
if struct.unpack_from("<H", image, 12)[0] != 9:
    p.error("The image is not for ESP32-S3")
if (version + "-field").encode() not in image:
    p.error("The image does not contain the current Field version")
public = subprocess.check_output(
    ["openssl", "pkey", "-in", str(a.key), "-pubout"], stderr=subprocess.DEVNULL
).strip()
expected = (
    re.search(r'R"KEY\((.*?)\)KEY"', (ROOT / "src/FieldUpdateKey.h").read_text(), re.S)
    .group(1)
    .encode()
    .strip()
)
if public != expected:
    p.error("Signing key does not match the firmware verification key")
manifest = {
    "schema_version": 1,
    "version": version,
    "version_code": code,
    "board": "t-deck-plus-16mb",
    "chip": "esp32s3",
    "image_type": "app",
    "size": len(image),
    "sha256": hashlib.sha256(image).hexdigest(),
    "url": f"https://github.com/Aleph72/MeshDeck/releases/download/{a.tag}/firmware-t-deck-plus.bin",
    "source_commit": subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True
    ).strip(),
}
payload = (json.dumps(manifest, separators=(",", ":"), sort_keys=True) + "\n").encode()
signature = subprocess.check_output(
    ["openssl", "dgst", "-sha256", "-sign", str(a.key)], input=payload
)
envelope = {
    "payload": base64.b64encode(payload).decode(),
    "signature": base64.b64encode(signature).decode(),
}
stable = (json.dumps(envelope, separators=(",", ":")) + "\n").encode()
if len(stable) > 2048:
    p.error("Signed update envelope exceeds the device limit")
a.output.mkdir(parents=True)
(a.output / "firmware-t-deck-plus.bin").write_bytes(image)
(a.output / "manifest.json").write_bytes(payload)
(a.output / "manifest.sig").write_bytes(signature)
(a.output / "stable.json").write_bytes(stable)
(a.output / "SHA256SUMS").write_text(
    "\n".join(
        hashlib.sha256((a.output / name).read_bytes()).hexdigest() + "  " + name
        for name in (
            "firmware-t-deck-plus.bin",
            "manifest.json",
            "manifest.sig",
            "stable.json",
        )
    )
    + "\n"
)
print("Signed release prepared:", a.tag, version, len(image), "bytes")
