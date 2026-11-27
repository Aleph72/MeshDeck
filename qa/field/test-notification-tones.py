import atexit
import io
import re
import shutil
import struct
import subprocess
import tempfile
import wave
from pathlib import Path

base = Path(__file__).resolve().parents[2]
tmp = Path(tempfile.mkdtemp(prefix="meshdeck-tone-test-"))
atexit.register(shutil.rmtree, tmp)
s = (base / "src/TDeckPop.cpp").read_text()
body = s[s.index("static std::atomic<int>") :]
stub = r"""
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#define HAS_I2S 1
#define T_DECK 1
#include "TDeckNotificationTone.h"
#include "NotificationToneWav.h"
static const uint8_t kPopWav[]={0};
static constexpr unsigned kPopWavLen=sizeof(kPopWav);
constexpr int meshtastic_Config_DeviceConfig_BuzzerMode_DISABLED=1;
struct { struct { int buzzer_mode=0; } device; } config;
int volume=63; bool alarm=false;
extern "C" int tdeck_volume_get() { return volume; }
extern "C" bool tdeck_alarm_ringing() { return alarm; }
struct Preferences {
 static std::map<std::string,int> values;
 bool begin(const char*,bool) { return true; }
 int getUChar(const char*key,int fallback) { return values.count(key)?values[key]:fallback; }
 int putUChar(const char*key,int value) { values[key]=value;return 1; }
 void end() {}
};
std::map<std::string,int> Preferences::values;
struct Audio {
 const void *last=nullptr; bool wav=false,rtttl=false; unsigned starts=0;
 bool wavPlaying() { return wav; } bool rtttlPlaying() { return rtttl; }
 void beginWav(const void *p,unsigned) { last=p;++starts; }
} audio,*audioThread=&audio;
struct MeshPacket {
 uint32_t from,to,channel; int which_payload_variant;
 struct { int portnum; struct {char bytes[2];unsigned size;} payload;} decoded;
};
using meshtastic_MeshPacket=MeshPacket;
#define meshtastic_MeshPacket_init_default {}
constexpr int meshtastic_MeshPacket_decoded_tag=1,meshtastic_PortNum_TEXT_MESSAGE_APP=1;
struct Node { uint32_t getNodeNum(){return 0xd8bcb9e4;} } node,*nodeDB=&node;
struct Module { unsigned called=0; void testNotification() { ++called;tdeck_notification_request(); } } module,*externalNotificationModule=&module;
"""
tests = r"""
int main() {
 Preferences::values["volume"]=63;
 tdeck_pop_service(); assert(tdeck_notification_tone_get()==0);
 tdeck_pop_request();tdeck_pop_service();assert(audio.last==kPopWav);
 const void* expected[]={kPopWav,kNotificationTone1,kNotificationTone2,kNotificationTone3};
 for(int tone=1;tone<=3;++tone) {
  tdeck_notification_tone_set(tone);tdeck_pop_service();assert(tdeck_notification_tone_get()==tone);
  assert(Preferences::values["ringtone"]==tone);assert(Preferences::values["volume"]==63);
  unsigned n=audio.starts;tdeck_pop_request();tdeck_pop_service();assert(audio.starts==n);
  tdeck_notification_tone_test();tdeck_pop_service();assert(audio.starts==n+1&&audio.last==expected[tone]);
  tdeck_notification_test_message();tdeck_pop_service();assert(audio.starts==n+2&&module.called==unsigned(tone));
  s_loaded=false;s_tone.store(0);tdeck_pop_service();assert(tdeck_notification_tone_get()==tone);
 }
 tdeck_notification_tone_set(-1);tdeck_pop_service();assert(tdeck_notification_tone_get()==3);
 tdeck_notification_tone_set(4);tdeck_pop_service();assert(tdeck_notification_tone_get()==3);
 unsigned n=audio.starts;
 config.device.buzzer_mode=1;tdeck_notification_tone_test();tdeck_pop_service();assert(audio.starts==n);
 tdeck_notification_request();tdeck_pop_service();assert(audio.starts==n);
 config.device.buzzer_mode=0;volume=0;tdeck_notification_tone_test();tdeck_pop_service();assert(audio.starts==n);
 volume=63;audio.wav=true;tdeck_notification_request();tdeck_pop_service();assert(audio.starts==n);
 audio.wav=false;audio.rtttl=true;tdeck_notification_request();tdeck_pop_service();assert(audio.starts==n);
 audio.rtttl=false;alarm=true;tdeck_notification_request();tdeck_pop_service();assert(audio.starts==n);
 alarm=false;tdeck_notification_request();tdeck_pop_service();assert(audio.starts==n+1);
 Preferences::values["ringtone"]=255;s_loaded=false;tdeck_pop_service();assert(tdeck_notification_tone_get()==0);
 assert(Preferences::values["volume"]==63);
 puts("PASS: preset selection, persistence, original fallback, invalid settings, mute, zero volume, busy audio, alarm priority and local message routing.");
}
"""
(tmp / "test-tones.cpp").write_text(stub + body + tests)
subprocess.run(
    [
        "clang++",
        "-std=c++17",
        "-fsanitize=address,undefined",
        "-I" + str(base / "src"),
        str(tmp / "test-tones.cpp"),
        "-o",
        str(tmp / "test-tones"),
    ],
    check=True,
)
subprocess.run([str(tmp / "test-tones")], check=True)
for i in (1, 2, 3):
    header = (base / "src/NotificationToneWav.h").read_text()
    content = re.search(
        r"kNotificationTone" + str(i) + r"\[\] = \{(.*?)\};", header, re.S
    ).group(1)
    data = bytes(int(v, 16) for v in re.findall(r"0x([0-9a-f]{2})", content))
    with wave.open(io.BytesIO(data), "rb") as w:
        assert (
            w.getnchannels() == 1
            and w.getframerate() == 16000
            and w.getsampwidth() == 2
        )
        assert 0 < w.getnframes() / 16000 < 1
        samples = struct.unpack(
            "<" + "h" * w.getnframes(), w.readframes(w.getnframes())
        )
        assert samples[0] == samples[-1] == 0 and max(map(abs, samples)) < 32767
print(
    "PASS: all three WAV files are bounded, mono 16-bit 16 kHz, fade to silence, and do not clip."
)
