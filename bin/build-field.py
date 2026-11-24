#!/usr/bin/env python3
"""Project-local macOS/Linux build bootstrap for T-Deck Field."""

import os
import pathlib
import shutil
import subprocess
import sys

if sys.version_info < (3, 10):
    raise SystemExit("Python 3.10 or newer is required")
root = pathlib.Path(__file__).resolve().parents[1]
work = root / ".field-build"
work.mkdir(exist_ok=True)
for name in ("tmp", "pip-cache", "pio-cache"):
    (work / name).mkdir(exist_ok=True)
env = os.environ.copy()
env.update(
    PLATFORMIO_CORE_DIR=str(work / "core"),
    PLATFORMIO_CACHE_DIR=str(work / "pio-cache"),
    PIP_CACHE_DIR=str(work / "pip-cache"),
    TMPDIR=str(work / "tmp"),
)
venv = work / "venv"
python = venv / "bin/python"
if not python.exists():
    subprocess.run([sys.executable, "-m", "venv", str(venv)], check=True)
subprocess.run(
    [str(python), "-m", "pip", "install", "platformio==6.1.19"], env=env, check=True
)
subprocess.run(
    [str(python), "-m", "platformio", "pkg", "install", "-e", "t-deck-tft"],
    cwd=root,
    env=env,
    check=True,
)
platform = work / "core/platforms/espressif32"
helper = platform / "platform.py"
text = helper.read_text()
line = next(
    (
        line
        for line in text.splitlines(keepends=True)
        if line.strip() == '"tool-scons",'
    ),
    None,
)
if line:
    if text.count(line) != 1:
        raise SystemExit(
            "Unexpected platform helper; inspect the SCons workaround manually"
        )
    helper.write_text(text.replace(line, ""))
for name in (
    "arduino-lib-builder-gcc.c",
    "arduino-lib-builder-cpp.cpp",
    "arduino-lib-builder-as.S",
):
    target = root / ".dummy" / name
    if not target.exists():
        shutil.copy2(platform / "builder/build_lib" / name, target)
managed = work / "core/penv/bin/python"
if managed.exists():
    python = managed
subprocess.run(
    [str(python), "-m", "platformio", "run", "-e", "t-deck-tft"],
    cwd=root,
    env=env,
    check=True,
)
