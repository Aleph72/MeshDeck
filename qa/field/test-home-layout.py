"""Exercise the real Home layout service and NVS bridge, including fresh-process reloads."""

import os
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]
work = root / ".field-build/host-tests"
work.mkdir(parents=True, exist_ok=True)
fixture = r"""
#include <cassert>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include "util/HomeLayout.h"
std::vector<char> storage;
bool failOpen = false, failWrite = false;
unsigned writes = 0;
class Preferences {
public:
 bool begin(const char *name, bool) { assert(!strcmp(name, "meshhome")); return !failOpen; }
 size_t getBytesLength(const char *key) { assert(!strcmp(key, "layout")); return storage.size(); }
 size_t getBytes(const char *, void *out, size_t size) {
  if(size != storage.size()) return 0;
  memcpy(out, storage.data(), size); return size;
 }
 size_t putBytes(const char *key, const void *data, size_t size) {
  assert(!strcmp(key, "layout")); if(failWrite) return 0;
  auto *p=static_cast<const char *>(data); storage.assign(p,p+size); ++writes; return size;
 }
 void end() {}
};
#include HOME_PREFS_SOURCE
using namespace meshhome;
static_assert(sizeof(Chat)==8 && sizeof(Layout)==1260);
int main(int argc, char **argv) {
 assert(argc==3); const std::string mode=argv[1];
 std::ifstream in(argv[2],std::ios::binary); storage.assign(std::istreambuf_iterator<char>(in),{});
 if(mode=="seed") {
  assert(!saved() && layout().pinCount==0 && writes==0);
  char legacy[2][kKeySize]={"Mesh","Maps"}; seedLegacyOrder(legacy,2);
  assert(layout().orderCount==2 && !saved() && writes==0);
  assert(setPinned({0,Kind::Channel,{}},true));
  assert(setPinned({0x8abc1234,Kind::Direct,{}},true));
  assert(saved() && layout().pinCount==2 && writes==2);
  assert(setPinned({0,Kind::Channel,{}},true) && writes==2);
  char keys[4][kKeySize]={"Mesh","Maps",{}, {}};
  chatKey({0,Kind::Channel,{}},keys[2]);chatKey({0x8abc1234,Kind::Direct,{}},keys[3]);
  assert(strcmp(keys[2],keys[3]) && setOrder(keys,4) && writes==3);
  assert(setOrder(keys,4) && writes==3);
 } else if(mode=="reload" || mode=="fail" || mode=="limits") {
  assert(saved() && validLayout(layout()) && writes==0);
  assert(layout().pinCount==2 && layout().orderCount==4);
  assert(layout().pins[1].id==0x8abc1234 && !strcmp(layout().order[0],"Mesh"));
  assert(!setPinned({8,Kind::Channel,{}},true));
  assert(!setPinned({0,Kind::Direct,{}},true));
  assert(!setPinned({UINT32_MAX,Kind::Direct,{}},true));
  assert(!setOrder(nullptr,kMaxOrder+1));
  if(mode=="fail") {
   Layout before=layout();failWrite=true;
   assert(!setPinned({1,Kind::Channel,{}},true));
   assert(!memcmp(&before,&layout(),sizeof(before)) && writes==0);
   char keys[1][kKeySize]={"Maps"};assert(!setOrder(keys,1));
   assert(!memcmp(&before,&layout(),sizeof(before)));
   failWrite=false;failOpen=true;
   assert(!setPinned({1,Kind::Channel,{}},true));
   assert(!memcmp(&before,&layout(),sizeof(before)) && writes==0);
  }
  if(mode=="limits") {
   for(unsigned i=2;i<kMaxPins;++i)assert(setPinned({0x10000000+i,Kind::Direct,{}},true));
   assert(layout().pinCount==kMaxPins);
   auto n=writes; assert(!setPinned({0x20000000,Kind::Direct,{}},true) && writes==n);
   assert(setPinned({0,Kind::Channel,{}},false));
   assert(layout().pinCount==kMaxPins-1 && layout().orderCount==3);
   char key[kKeySize];chatKey({0,Kind::Channel,{}},key);
   for(unsigned i=0;i<layout().orderCount;++i)assert(strcmp(key,layout().order[i]));
   char duplicate[2][kKeySize]={"Mesh","Mesh"};n=writes;
   assert(!setOrder(duplicate,2) && writes==n);
   char broken[1][kKeySize];memset(broken,'x',sizeof(broken));
   assert(!setOrder(broken,1) && writes==n);
   char newline[1][kKeySize]={"Mesh\nMaps"};assert(!setOrder(newline,1));
  }
 } else if(mode=="corrupt") {
  assert(!saved() && layout().pinCount==0 && layout().orderCount==0 && writes==0);
 } else assert(false);
 if(writes) { std::ofstream out(argv[2],std::ios::binary);out.write(storage.data(),storage.size()); }
}
"""
with tempfile.TemporaryDirectory(dir=work) as tmp:
    tmp = Path(tmp)
    (tmp / "Preferences.h").write_text("#pragma once\n")
    (tmp / "main.cpp").write_text(fixture)
    executable = tmp / "home-test"
    subprocess.run(
        [
            "clang++",
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-fsanitize=address,undefined",
            '-DHOME_PREFS_SOURCE="' + str(root / "src/TDeckHomePrefs.cpp") + '"',
            "-I" + str(tmp),
            "-I" + str(root / "lib/meshtastic-device-ui/include"),
            str(tmp / "main.cpp"),
            str(root / "lib/meshtastic-device-ui/source/HomeLayout.cpp"),
            "-o",
            str(executable),
        ],
        check=True,
        env=os.environ.copy(),
    )
    saved = tmp / "nvs.bin"
    for mode in ("seed", "reload", "fail"):
        subprocess.run([str(executable), mode, str(saved)], check=True)
    limits = tmp / "limits.bin"
    limits.write_bytes(saved.read_bytes())
    subprocess.run([str(executable), "limits", str(limits)], check=True)
    for content in (b"", saved.read_bytes()[:-1], saved.read_bytes() + b"x"):
        broken = tmp / "broken.bin"
        broken.write_bytes(content)
        subprocess.run([str(executable), "corrupt", str(broken)], check=True)
    damaged = bytearray(saved.read_bytes())
    damaged[10] ^= 1
    broken.write_bytes(damaged)
    subprocess.run([str(executable), "corrupt", str(broken)], check=True)
print(
    "PASS: Home pins/order, fresh-process reload, legacy seed, limits, corrupt NVS and save-failure rollback; ASan/UBSan."
)
