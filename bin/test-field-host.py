#!/usr/bin/env python3
"""Run bounded parser tests with Clang sanitizers against the actual firmware source."""

import argparse
import json
import os
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument(
    "--hearham",
    type=Path,
    required=True,
    help="Public JSON snapshot from https://hearham.com/api/repeaters/v1",
)
args = parser.parse_args()
core = Path(os.environ.get("PLATFORMIO_CORE_DIR", root / ".field-build/core"))
cjson = core / "packages/framework-espidf/components/json/cJSON"
if not (cjson / "cJSON.c").exists():
    parser.error(
        "Build first, or set PLATFORMIO_CORE_DIR to the tested PlatformIO core directory"
    )
count = len(json.loads(args.hearham.read_text()))
work = root / ".field-build/host-tests"
work.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(dir=work) as tmp:
    out = Path(tmp)
    flags = ["-g", "-fsanitize=address,undefined", "-I" + str(cjson)]
    subprocess.run(
        ["clang", *flags, "-c", str(cjson / "cJSON.c"), "-o", str(out / "cjson.o")],
        check=True,
    )
    for template, source, begin, end, marker in (
        (
            "reply-repeater",
            "src/TDeckRepeaters.cpp",
            "uint32_t checksum(",
            "void download(",
            "// @REPEATER_IMPLEMENTATION@",
        ),
        (
            "manifest",
            "src/FieldUpdater.cpp",
            "struct Manifest",
            "class FirmwareStream",
            "// @OTA_IMPLEMENTATION@",
        ),
    ):
        code = (root / source).read_text()
        extracted = code[code.index(begin) : code.index(end)]
        fixture = (
            (root / "qa/field" / (template + ".cpp.in"))
            .read_text()
            .replace(marker, extracted)
        )
        cpp = out / (template + ".cpp")
        cpp.write_text(fixture)
        executable = out / template
        subprocess.run(
            [
                "clang++",
                "-std=c++17",
                *flags,
                "-I" + str(root / "lib/meshtastic-device-ui/include"),
                str(cpp),
                str(out / "cjson.o"),
                "-o",
                str(executable),
            ],
            check=True,
        )
        command = [str(executable)]
        if template == "reply-repeater":
            command.extend([str(args.hearham.resolve()), str(count)])
        subprocess.run(command, check=True)
