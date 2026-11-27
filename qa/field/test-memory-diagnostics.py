"""Exercise the actual memory readout and firmware bridges with host counters."""

import os
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]
ui = (
    root / "lib/meshtastic-device-ui/source/graphics/TFT/TFTView_320x240.cpp"
).read_text()
readout = ui[ui.index("void TFTView_320x240::updateFreeMem(void)") :]
readout = readout[: readout.index("void TFTView_320x240::task_handler(void)")]
diagnostic = ui[ui.index("    char buf[512], uptimeText[21];") :]
diagnostic = diagnostic[: diagnostic.index("    lv_label_set_text(body, buf);")]
remote = ui[ui.index("        char uptime[21];") :]
remote = remote[: remote.index("        tdeck_remote_reply(buf);")]
assert "%ll" not in diagnostic + remote
firmware = (root / "src/TDeckMemInfo.cpp").read_text()
bridges = firmware[firmware.index('extern "C" uint32_t tdeck_free_heap(void)') :]
bridges = bridges[: bridges.index("// ---- restart reason")]
stub = r"""
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#include <cstring>
#include "util/UptimeFormat.h"
#define _(s) s
struct Esp {
    uint32_t free = 69396, total = 277584, low = 65000, biggest = 31732;
    uint32_t getFreeHeap() { return free; }
    uint32_t getHeapSize() { return total; }
    uint32_t getMinFreeHeap() { return low; }
    uint32_t getMaxAllocHeap() { return biggest; }
    uint32_t getFreePsram() { return 2535712; }
    uint32_t getMinFreePsram() { return 2400000; }
} ESP;
int64_t nowUs = 0;
int64_t esp_timer_get_time() { return nowUs; }
struct LinuxHelper {
    static uint32_t getTotalMem() { return ESP.total; }
    static uint32_t getAvailableMem() { return ESP.free; }
};
struct Object { void *user_data = nullptr; } panel, button, label, other;
struct {
    Object *home_panel = &panel, *home_memory_button = &button, *home_memory_label = &label;
} objects;
struct lv_mem_monitor_t { uint32_t free_size; uint8_t used_pct; uint32_t free_biggest_size = 3300000; uint8_t frag_pct = 1; };
unsigned samples = 0;
void lv_mem_monitor(lv_mem_monitor_t *m) { ++samples; *m = {3391800, 35}; }
std::string text;
void lv_label_set_text(Object *, const char *s) { text = s; }
struct TFTView_320x240 {
    Object *activePanel = &panel;
    void updateFreeMem(void);
    int lockState = 0;
};
"""
format_stubs = (
    r"""
const char *tdeck_prev_reason_str() { return "usb reset"; }
uint32_t tdeck_prev_heap_low() { return 60000; }
uint32_t tdeck_prev_psram_low() { return 2400000; }
std::string diagnosticText() {
    char stall[112] = "";
    lv_mem_monitor_t mon; lv_mem_monitor(&mon);
"""
    + diagnostic
    + r"""
    return buf;
}
std::string remoteText() {
    char buf[176]; const char *name = "launcher";
    TFTView_320x240 instance; auto *THIS = &instance;
    lv_mem_monitor_t lvm; lv_mem_monitor(&lvm);
"""
    + remote
    + r"""
    return buf;
}
"""
)
tests = r"""
int main() {
    char uptime[21];
    for (uint64_t seconds : {uint64_t(0), uint64_t(8640000), uint64_t(4294967296), UINT64_MAX})
        assert(std::string(tdeckFormatUptime(uptime, seconds)) == std::to_string(seconds));
    TFTView_320x240 view;
    view.updateFreeMem();
    assert(samples == 0 && text.empty());
    button.user_data = reinterpret_cast<void *>(1);
    view.activePanel = &other;
    view.updateFreeMem();
    assert(samples == 0);
    view.activePanel = &panel;
    view.updateFreeMem();
#ifdef ARDUINO_ARCH_ESP32
    assert(text == "Heap: 69396 (25%)\nLVGL: 3391800 (65%)");
    ESP.total = 0;
    view.updateFreeMem();
    assert(text == "Heap: 69396 (0%)\nLVGL: 3391800 (65%)");
#else
    assert(text == "Heap: 69396 (25%)\nLVGL: 3312 (65%)");
#endif
    ESP.total = 4000000000u;
    ESP.free = 3000000000u;
    view.updateFreeMem();
#ifdef ARDUINO_ARCH_ESP32
    assert(text.find("(75%)") != std::string::npos);
#else
    assert(text.find("(" + std::to_string(uint64_t(100) * ESP.free / 277584) + "%)") != std::string::npos);
#endif
    ESP.free = 69396;
    assert(tdeck_free_heap() == 69396 && tdeck_min_free_heap() == 65000);
    assert(tdeck_largest_free_heap() == 31732);
    assert(tdeck_free_psram() == 2535712 && tdeck_min_free_psram() == 2400000);
    nowUs = 999999;
    assert(tdeck_uptime_seconds() == 0);
    nowUs = 4294967296LL * 1000;
    assert(tdeck_uptime_seconds() == 4294967);
    nowUs = 100LL * 86400 * 1000000;
    assert(tdeck_uptime_seconds() == 8640000);
    assert(diagnosticText().find("Uptime: 8640000s\nLast restart: usb reset") == 0);
    assert(remoteText().find("info screen=launcher uptime=8640000s lock=0") == 0);
    assert(remoteText().find("lvfrag=1%") != std::string::npos);
    puts("PASS: target-safe diagnostic/USB formatting, uint64 range, free bytes/percent, disabled/hidden monitor, safe percentage arithmetic, heap bridges and 100-day uptime");
}
"""
work = root / ".field-build/host-tests"
work.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(dir=work) as tmp:
    cpp = Path(tmp) / "memory.cpp"
    cpp.write_text(stub + bridges + readout + format_stubs + tests)
    for arch in ("ARDUINO_ARCH_ESP32", "ARCH_PORTDUINO"):
        executable = Path(tmp) / arch
        subprocess.run(
            [
                "clang++",
                "-std=c++17",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-fsanitize=address,undefined",
                "-D" + arch,
                "-I" + str(root / "lib/meshtastic-device-ui/include"),
                str(cpp),
                "-o",
                str(executable),
            ],
            check=True,
            env=os.environ.copy(),
        )
        subprocess.run([str(executable)], check=True)
