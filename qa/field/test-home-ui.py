"""Build the real launcher methods and Home chooser against an existing native LVGL library."""

import argparse
import os
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("--output", type=Path, required=True)
parser.add_argument("--lvgl-library", type=Path, required=True)
parser.add_argument("--lvgl-config", type=Path, required=True)
parser.add_argument("--sanitize-lvgl", action="store_true")
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
ui = root / "lib/meshtastic-device-ui"
core = Path(os.environ["PLATFORMIO_CORE_DIR"])
output = args.output.resolve()
output.mkdir(parents=True, exist_ok=True)
source = (ui / "source/graphics/TFT/TFTView_320x240.cpp").read_text()
methods = []
for name in [
    "applyAppOrder",
    "saveAppOrder",
    "buildAppGrid",
    "rebuildAppGrid",
    "closeArrange",
    "launcherTitle",
    "buildHomeControls",
    "refreshHomeBadges",
    "openHomeChat",
    "tuiHomeChanged",
    "refreshArrangeRows",
    "moveAppInArrange",
    "openArrange",
]:
    start = source.index("TFTView_320x240::" + name + "(")
    start = source.rfind("\n", 0, start) + 1
    end = source.index("\n}\n", start) + 3
    methods.append(source[start:end])
(output / "launcher-methods.inc").write_text("\n".join(methods))
start = source.index("struct LauncherApp {")
end = source.index("// --- simple per-app icons", start)
(output / "launcher-table.inc").write_text(source[start:end])
lvgl = root.parent / "source/.pio/libdeps/t-deck-tft/lvgl"
cmake = core / "packages/tool-cmake/bin/cmake"
ninja = core / "packages/tool-ninja/ninja"
fonts = [
    ui / f"generated/ui_320x240/ui_font_montserrat_{size}.c"
    for size in (12, 14, 16, 20)
]
sources = [
    root / "qa/field/home-ui-fixture.cpp",
    ui / "source/HomeLayout.cpp",
    ui / "source/graphics/TFT/HomeChats.cpp",
    ui / "source/graphics/TFT/TuiLabel.cpp",
    ui / "source/graphics/TFT/UnreadCounts.cpp",
] + fonts
cmake_text = f'''cmake_minimum_required(VERSION 3.20)
project(home_ui_test LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
add_compile_definitions(RAM_SIZE=8192 USE_FRAMEBUFFER=0 USE_X11=0 LV_KCONFIG_IGNORE LV_CONF_PATH="{args.lvgl_config.resolve()}")
add_executable(home-test {" ".join('"' + str(p) + '"' for p in sources)})
target_compile_options(home-test PRIVATE -g -UNDEBUG -fsanitize=address,undefined)
target_link_options(home-test PRIVATE -fsanitize=address,undefined)
target_include_directories(home-test PRIVATE "{output}" "{lvgl}" "{ui / "include"}")
target_link_libraries(home-test PRIVATE "{args.lvgl_library.resolve()}")
'''
if args.sanitize_lvgl:
    config = output / "lv-conf-asan.h"
    configuration = args.lvgl_config.read_text().replace(
        "LV_USE_STDLIB_MALLOC    LV_STDLIB_BUILTIN",
        "LV_USE_STDLIB_MALLOC    LV_STDLIB_CLIB",
    )
    if not config.exists() or config.read_text() != configuration:
        config.write_text(configuration)
    cmake_text = cmake_text.replace(str(args.lvgl_config.resolve()), str(config))
    extra = f'''add_compile_options(-g -fsanitize=address,undefined)
set(LV_BUILD_CONF_PATH "{config}" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory("{lvgl}" lvgl)
'''
    cmake_text = cmake_text.replace(
        "add_executable(home-test", extra + "add_executable(home-test"
    )
    cmake_text = cmake_text.replace(
        '"' + str(args.lvgl_library.resolve()) + '"', "lvgl"
    )
(output / "CMakeLists.txt").write_text(cmake_text)
subprocess.run(
    [
        str(cmake),
        "-S",
        str(output),
        "-B",
        str(output / "build"),
        "-G",
        "Ninja",
        "-DCMAKE_MAKE_PROGRAM=" + str(ninja),
    ],
    check=True,
)
subprocess.run([str(cmake), "--build", str(output / "build"), "-j4"], check=True)
subprocess.run([str(output / "build/home-test")], cwd=output, check=True)
