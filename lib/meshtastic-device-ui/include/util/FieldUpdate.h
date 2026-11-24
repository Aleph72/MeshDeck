#pragma once
#include <stddef.h>
#include <stdint.h>
constexpr uint32_t kFieldVersionCode = 2026112401u;
#define FIELD_APP_VERSION "2026.11.24.1"
constexpr const char *kFieldVersion = FIELD_APP_VERSION;
extern "C" {
void tdeck_ota_request(void);
void tdeck_ota_service(void);
void tdeck_ota_set_auto(bool enabled);
bool tdeck_ota_get_auto(void);
bool tdeck_ota_busy(void);
bool tdeck_ota_selftest(void);
unsigned tdeck_ota_progress(void);
void tdeck_ota_status(char *out, size_t cap);
bool tdeck_field_is_idle(void);
void updates_open(void);
}
