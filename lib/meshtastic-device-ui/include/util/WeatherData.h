#pragma once
#include <cstddef>
#include <cstdint>

namespace meshweather
{
constexpr unsigned kDays = 5, kPlaces = 5;
struct Place {
    char name[48], region[48], country[4];
    double latitude, longitude;
};
struct Day {
    char date[11];
    float low, high;
    int16_t code, rain;
};
struct Forecast {
    Place place;
    char updated[20];
    uint32_t fetched;
    float temperature, apparent, wind;
    int16_t humidity, code;
    uint8_t count;
    Day days[kDays];
};
void init();
void service();
bool refresh(bool manual = true);
bool search(const char *query);
bool choose(unsigned index);
bool busy();
unsigned generation();
const char *status();
const Forecast *forecast();
unsigned placeCount();
const Place *place(unsigned index);
const char *description(int code);
} // namespace meshweather

extern "C" void weather_open(void);
extern "C" bool weather_back(void);
