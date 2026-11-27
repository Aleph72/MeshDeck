#pragma once
#include "util/WeatherData.h"
#include <cJSON.h>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace meshweather
{
inline bool boundedJSON(const char *body, size_t length)
{
    if (!body || !length || length > 8192)
        return false;
    unsigned depth = 0;
    bool quoted = false, escaped = false;
    for (size_t i = 0; i < length; ++i) {
        const char c = body[i];
        if (!c)
            return false;
        if (quoted) {
            if (escaped)
                escaped = false;
            else if (c == '\\')
                escaped = true;
            else if (c == '"')
                quoted = false;
        } else if (c == '"') {
            quoted = true;
        } else if (c == '{' || c == '[') {
            if (++depth > 16)
                return false;
        } else if (c == '}' || c == ']') {
            if (!depth)
                return false;
            --depth;
        }
    }
    return !quoted && !depth;
}

inline bool number(cJSON *value, double low, double high, double &out)
{
    if (!cJSON_IsNumber(value) || !std::isfinite(value->valuedouble) || value->valuedouble < low || value->valuedouble > high)
        return false;
    out = value->valuedouble;
    return true;
}

inline cJSON *parseJSON(const char *body, size_t length)
{
    if (!boundedJSON(body, length))
        return nullptr;
    const char *end = nullptr;
    cJSON *root = cJSON_ParseWithLengthOpts(body, length, &end, false);
    if (!root)
        return nullptr;
    while (end < body + length && std::isspace(static_cast<unsigned char>(*end)))
        ++end;
    if (end != body + length) {
        cJSON_Delete(root);
        return nullptr;
    }
    return root;
}

inline bool text(cJSON *value, char *out, size_t capacity)
{
    if (!cJSON_IsString(value) || !value->valuestring[0])
        return false;
    const char *s = value->valuestring;
    size_t n = strlen(s);
    if (n >= capacity) {
        n = capacity - 1;
        while (n && (static_cast<unsigned char>(s[n]) & 0xc0) == 0x80)
            --n;
    }
    memcpy(out, s, n);
    out[n] = 0;
    return n > 0;
}

inline bool date(const char *s)
{
    if (!s || strlen(s) != 10 || s[4] != '-' || s[7] != '-')
        return false;
    for (unsigned i = 0; i < 10; ++i)
        if (i != 4 && i != 7 && (s[i] < '0' || s[i] > '9'))
            return false;
    const int month = (s[5] - '0') * 10 + s[6] - '0', day = (s[8] - '0') * 10 + s[9] - '0';
    return month >= 1 && month <= 12 && day >= 1 && day <= 31;
}

inline bool validPlace(const Place &p)
{
    return memchr(p.name, 0, sizeof(p.name)) && p.name[0] && memchr(p.region, 0, sizeof(p.region)) &&
           memchr(p.country, 0, sizeof(p.country)) && std::isfinite(p.latitude) && std::isfinite(p.longitude) &&
           p.latitude >= -90 && p.latitude <= 90 && p.longitude >= -180 && p.longitude <= 180;
}

inline bool validForecast(const Forecast &f)
{
    if (!validPlace(f.place) || !memchr(f.updated, 0, sizeof(f.updated)) || strlen(f.updated) < 16 || f.count < 1 ||
        f.count > kDays || !std::isfinite(f.temperature) || !std::isfinite(f.apparent) || !std::isfinite(f.wind) ||
        f.temperature < -150 || f.temperature > 100 || f.apparent < -150 || f.apparent > 100 || f.wind < 0 || f.wind > 500 ||
        f.humidity < 0 || f.humidity > 100 || f.code < 0 || f.code > 99)
        return false;
    char day[11]{};
    memcpy(day, f.updated, 10);
    if (!date(day) || f.updated[10] != 'T' || f.updated[13] != ':' || f.updated[11] < '0' || f.updated[11] > '2' ||
        f.updated[12] < '0' || f.updated[12] > '9' || f.updated[14] < '0' || f.updated[14] > '5' || f.updated[15] < '0' ||
        f.updated[15] > '9' || (f.updated[11] == '2' && f.updated[12] > '3'))
        return false;
    for (unsigned i = 0; i < f.count; ++i) {
        const Day &d = f.days[i];
        if (!memchr(d.date, 0, sizeof(d.date)) || !date(d.date) || !std::isfinite(d.low) || !std::isfinite(d.high) ||
            d.low < -150 || d.high > 100 || d.low > d.high || d.rain < -1 || d.rain > 100 || d.code < 0 || d.code > 99)
            return false;
    }
    return true;
}

inline bool parseForecast(const char *body, size_t length, const Place &where, Forecast &out)
{
    if (!validPlace(where) || !boundedJSON(body, length))
        return false;
    cJSON *root = parseJSON(body, length);
    if (!root)
        return false;
    Forecast f{};
    f.place = where;
    cJSON *current = cJSON_GetObjectItemCaseSensitive(root, "current");
    cJSON *units = cJSON_GetObjectItemCaseSensitive(root, "current_units");
    cJSON *daily = cJSON_GetObjectItemCaseSensitive(root, "daily");
    auto item = [](cJSON *object, const char *key) { return cJSON_GetObjectItemCaseSensitive(object, key); };
    auto unit = [&](const char *key, const char *expected) {
        cJSON *v = item(units, key);
        return cJSON_IsString(v) && !strcmp(v->valuestring, expected);
    };
    double t = 0, a = 0, w = 0, h = 0, code = 0;
    bool ok = unit("temperature_2m", "°C") && unit("wind_speed_10m", "km/h") &&
              number(item(current, "temperature_2m"), -150, 100, t) &&
              number(item(current, "apparent_temperature"), -150, 100, a) && number(item(current, "wind_speed_10m"), 0, 500, w) &&
              number(item(current, "relative_humidity_2m"), 0, 100, h) && number(item(current, "weather_code"), 0, 99, code) &&
              text(item(current, "time"), f.updated, sizeof(f.updated));
    f.temperature = t;
    f.apparent = a;
    f.wind = w;
    f.humidity = h;
    f.code = code;
    cJSON *times = item(daily, "time"), *codes = item(daily, "weather_code");
    cJSON *lows = item(daily, "temperature_2m_min"), *highs = item(daily, "temperature_2m_max");
    cJSON *rain = item(daily, "precipitation_probability_max");
    const int count = cJSON_GetArraySize(times);
    ok = ok && count >= 1 && count <= static_cast<int>(kDays) && cJSON_GetArraySize(codes) == count &&
         cJSON_GetArraySize(lows) == count && cJSON_GetArraySize(highs) == count;
    if (ok) {
        f.count = count;
        for (int i = 0; i < count; ++i) {
            Day &d = f.days[i];
            double lo = 0, hi = 0, co = 0, pr = -1;
            ok = text(cJSON_GetArrayItem(times, i), d.date, sizeof(d.date)) && date(d.date) &&
                 number(cJSON_GetArrayItem(lows, i), -150, 100, lo) && number(cJSON_GetArrayItem(highs, i), -150, 100, hi) &&
                 number(cJSON_GetArrayItem(codes, i), 0, 99, co) && lo <= hi;
            if (!ok)
                break;
            number(cJSON_GetArrayItem(rain, i), 0, 100, pr);
            d.low = lo;
            d.high = hi;
            d.code = co;
            d.rain = pr;
        }
    }
    cJSON_Delete(root);
    if (!ok || !validForecast(f))
        return false;
    out = f;
    return true;
}

inline bool parsePlaces(const char *body, size_t length, Place *out, unsigned &count)
{
    count = 0;
    if (!out || !boundedJSON(body, length))
        return false;
    cJSON *root = parseJSON(body, length);
    if (!root)
        return false;
    cJSON *results = cJSON_GetObjectItemCaseSensitive(root, "results");
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return false;
    }
    for (int i = 0; i < cJSON_GetArraySize(results) && count < kPlaces; ++i) {
        cJSON *v = cJSON_GetArrayItem(results, i);
        Place p{};
        if (!text(cJSON_GetObjectItemCaseSensitive(v, "name"), p.name, sizeof(p.name)) ||
            !number(cJSON_GetObjectItemCaseSensitive(v, "latitude"), -90, 90, p.latitude) ||
            !number(cJSON_GetObjectItemCaseSensitive(v, "longitude"), -180, 180, p.longitude))
            continue;
        text(cJSON_GetObjectItemCaseSensitive(v, "admin1"), p.region, sizeof(p.region));
        text(cJSON_GetObjectItemCaseSensitive(v, "country_code"), p.country, sizeof(p.country));
        out[count++] = p;
    }
    bool ok = !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "error"));
    cJSON_Delete(root);
    return ok;
}
} // namespace meshweather
