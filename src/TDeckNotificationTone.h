#pragma once
#include <stddef.h>
extern "C" {
int tdeck_notification_tone_get(void);
void tdeck_notification_tone_set(int tone);
void tdeck_notification_tone_test(void);
bool tdeck_notification_uses_preset(void);
void tdeck_notification_request(void);
void tdeck_notification_test_message(void);
void tdeck_notification_tone_status(char *out, size_t cap);
}
