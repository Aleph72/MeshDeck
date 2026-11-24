#pragma once
#include <stdint.h>

struct RepeaterRecord {
    uint32_t id, frequencyHz;
    int32_t offsetHz, latitudeE7, longitudeE7;
    char callsign[16], city[64], mode[16], encode[24], decode[24], restriction[16];
    uint8_t operational, reserved[3];
};
static_assert(sizeof(RepeaterRecord) == 184, "Repeater cache format changed");

extern "C" {
void tdeck_repeaters_open(void);
void tdeck_repeaters_request_update(void);
void tdeck_repeaters_ui_service(void);
void tdeck_repeaters_use_zaragoza(void);
unsigned tdeck_repeaters_generation(void);
unsigned tdeck_repeaters_count(void);
unsigned tdeck_repeaters_near_count(void);
const RepeaterRecord *tdeck_repeaters_record(unsigned index);
uint32_t tdeck_repeaters_distance(unsigned index);
const char *tdeck_repeaters_status(void);
const char *tdeck_repeaters_reference(void);
bool tdeck_repeaters_busy(void);
}
