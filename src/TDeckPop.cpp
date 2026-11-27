#include "TDeckNotificationTone.h"
#include "main.h"
#include "mesh/NodeDB.h"
#include <Preferences.h>
#include <atomic>
#include <cstdio>
#ifdef HAS_I2S
#include "AudioThread.h"
#include "NotificationToneWav.h"
#include "modules/ExternalNotificationModule.h"
#include "pop_wav.h"
#endif

static std::atomic<int> s_tone{0};
static std::atomic<int> s_pendingTone{-1};
static std::atomic<bool> s_popPending{false};
static std::atomic<bool> s_messageTest{false};
static bool s_loaded = false;
static unsigned s_played = 0, s_muted = 0, s_busy = 0;
extern "C" int tdeck_volume_get(void);
extern "C" bool tdeck_alarm_ringing(void);

extern "C" int tdeck_notification_tone_get(void)
{
    return s_tone.load();
}
extern "C" void tdeck_notification_tone_set(int tone)
{
    if (tone >= 0 && tone <= 3)
        s_pendingTone.store(tone);
}
extern "C" bool tdeck_notification_uses_preset(void)
{
    return s_tone.load() != 0;
}
extern "C" void tdeck_notification_request(void)
{
    s_popPending.store(true);
}
extern "C" void tdeck_notification_tone_test(void)
{
    s_popPending.store(true);
}
extern "C" void tdeck_notification_test_message(void)
{
    s_messageTest.store(true);
}

extern "C" void tdeck_pop_request(void)
{
    // Presets are triggered by Meshtastic, which already applies channel/DM mute rules.
    if (!tdeck_notification_uses_preset())
        s_popPending.store(true);
}

extern "C" void tdeck_notification_tone_status(char *out, size_t cap)
{
    snprintf(out, cap, "tone=%d saved=%d volume=%d muted=%d played=%u suppressed=%u busy=%u", s_tone.load(),
             s_pendingTone.load() == -1, tdeck_volume_get(),
             config.device.buzzer_mode == meshtastic_Config_DeviceConfig_BuzzerMode_DISABLED, s_played, s_muted, s_busy);
}

extern "C" void tdeck_pop_service(void)
{
    if (!s_loaded) {
        Preferences prefs;
        if (prefs.begin("tui-audio", true)) {
            int tone = prefs.getUChar("ringtone", 0);
            s_tone.store(tone <= 3 ? tone : 0);
            prefs.end();
        }
        s_loaded = true;
    }
    int tone = s_pendingTone.load();
    if (tone >= 0) {
        Preferences prefs;
        if (prefs.begin("tui-audio", false)) {
            bool saved = prefs.putUChar("ringtone", tone) == 1;
            prefs.end();
            if (saved) {
                s_tone.store(tone);
                s_pendingTone.compare_exchange_strong(tone, -1);
            }
        }
    }
#if defined(HAS_I2S) && defined(T_DECK)
    if (s_messageTest.exchange(false) && externalNotificationModule)
        externalNotificationModule->testNotification();
#endif
    if (!s_popPending.exchange(false))
        return;
    if (config.device.buzzer_mode == meshtastic_Config_DeviceConfig_BuzzerMode_DISABLED || tdeck_volume_get() == 0) {
        ++s_muted;
        return;
    }
#ifdef HAS_I2S
    if (!audioThread)
        return;
    if (audioThread->wavPlaying() || audioThread->rtttlPlaying() || tdeck_alarm_ringing()) {
        ++s_busy;
        return;
    }
    switch (s_tone.load()) {
    case 1:
        audioThread->beginWav(kNotificationTone1, sizeof(kNotificationTone1));
        break;
    case 2:
        audioThread->beginWav(kNotificationTone2, sizeof(kNotificationTone2));
        break;
    case 3:
        audioThread->beginWav(kNotificationTone3, sizeof(kNotificationTone3));
        break;
    default:
        audioThread->beginWav(kPopWav, kPopWavLen);
        break;
    }
    ++s_played;
#endif
}
