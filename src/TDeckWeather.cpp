#include "HearHamCertificate.h"
#include "configuration.h"
#include "mesh/Throttle.h"
#include "util/FieldLocale.h"
#include "util/HttpBodyBuffer.h"
#include "util/WeatherParser.h"
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <time.h>

extern "C" bool tdeck_wifi_connected(void);
extern "C" bool tdeck_network_job_acquire(int);
extern "C" void tdeck_network_job_release(int);
extern "C" void tdeck_tls_reserve_release(void);
extern "C" void tdeck_tls_reserve_take(void);

namespace meshweather
{
namespace
{
constexpr int kJob = 4;
const Place defaultPlace{"Zaragoza", "Aragon", "ES", 41.65606, -0.87734};
struct Cache {
    uint32_t magic, checksum;
    Forecast value;
};
Forecast current{}, pending{};
Place choices[kPlaces]{}, pendingChoices[kPlaces]{}, requestedPlace{};
unsigned choicesCount = 0, pendingCount = 0, revision = 0;
bool loaded = false, haveCurrent = false, working = false, searchJob = false, success = false;
std::atomic<bool> done{false};
char query[65]{}, message[112]{};
uint32_t lastAttempt = 0;
int httpCode = 0;
const char *searchLanguage = "en";

uint32_t checksum(const Forecast &value)
{
    uint32_t h = 2166136261u;
    const auto *bytes = reinterpret_cast<const uint8_t *>(&value);
    for (size_t i = 0; i < sizeof(value); ++i)
        h = (h ^ bytes[i]) * 16777619u;
    return h;
}

void download(void *)
{
    char url[640]{};
    if (searchJob) {
        char encoded[193]{};
        size_t n = 0;
        for (const auto *p = reinterpret_cast<const unsigned char *>(query); *p; ++p) {
            const bool plain = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '-' ||
                               *p == '_' || *p == '.';
            if (plain)
                encoded[n++] = *p;
            else {
                snprintf(encoded + n, sizeof(encoded) - n, "%%%02X", *p);
                n += 3;
            }
        }
        snprintf(url, sizeof(url), "https://geocoding-api.open-meteo.com/v1/search?name=%s&count=5&language=%s&format=json",
                 encoded, searchLanguage);
    } else {
        snprintf(url, sizeof(url),
                 "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
                 "&current=temperature_2m,apparent_temperature,relative_humidity_2m,weather_code,wind_speed_10m"
                 "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max"
                 "&forecast_days=5&timezone=auto&temperature_unit=celsius&wind_speed_unit=kmh",
                 requestedPlace.latitude, requestedPlace.longitude);
    }
    char *body = static_cast<char *>(heap_caps_malloc(8193, MALLOC_CAP_SPIRAM));
    int length = 0;
    httpCode = 0;
    if (body) {
        tdeck_tls_reserve_release();
        {
            WiFiClientSecure client;
            client.setCACert(kHearHamRootCA);
            client.setHandshakeTimeout(10);
            HTTPClient http;
            http.setConnectTimeout(8000);
            http.setTimeout(8000);
            http.setReuse(false);
            http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
            http.setUserAgent("MeshDeck-Meteo/1.0");
            if (http.begin(client, url)) {
                httpCode = http.GET();
                if (httpCode == 200) {
                    HttpBodyBuffer sink(reinterpret_cast<uint8_t *>(body), 8192);
                    const int n = http.writeToStream(&sink);
                    if (n > 0 && !sink.overflowed() && static_cast<size_t>(n) == sink.size()) {
                        body[n] = 0;
                        length = n;
                    }
                }
                http.end();
            }
        }
        if (length)
            success = searchJob ? parsePlaces(body, length, pendingChoices, pendingCount)
                                : parseForecast(body, length, requestedPlace, pending);
        heap_caps_free(body);
        tdeck_tls_reserve_take();
    }
    tdeck_network_job_release(kJob);
    done.store(true, std::memory_order_release);
    vTaskDelete(nullptr);
}

bool start(bool searchRequest)
{
    if (working)
        return false;
    if (!tdeck_wifi_connected()) {
        snprintf(message, sizeof(message), "%s",
                 fieldText("Wi-Fi disconnected. Connect in Settings.", "Wi-Fi scollegato. Connettilo nelle impostazioni.",
                           "Wi-Fi desconectado. Conéctalo en Ajustes."));
        ++revision;
        return false;
    }
    if (!tdeck_network_job_acquire(kJob)) {
        snprintf(message, sizeof(message), "%s",
                 fieldText("Network busy. Try again shortly.", "Rete occupata. Riprova tra poco.",
                           "Red ocupada. Inténtalo de nuevo."));
        ++revision;
        return false;
    }
    searchJob = searchRequest;
    searchLanguage = fieldText("en", "it", "es");
    working = true;
    success = false;
    pendingCount = 0;
    done.store(false, std::memory_order_relaxed);
    lastAttempt = millis() | 1;
    snprintf(message, sizeof(message), "%s",
             searchJob ? fieldText("Searching for cities...", "Ricerca città...", "Buscando ciudades...")
                       : fieldText("Updating weather...", "Aggiornamento meteo...", "Actualizando el tiempo..."));
    ++revision;
    if (xTaskCreate(download, "meteo", 8192, nullptr, 1, nullptr) != pdPASS) {
        working = false;
        tdeck_network_job_release(kJob);
        snprintf(message, sizeof(message), "%s",
                 fieldText("Out of memory. Try again.", "Memoria insufficiente. Riprova.",
                           "Memoria insuficiente. Inténtalo de nuevo."));
        ++revision;
        return false;
    }
    return true;
}
} // namespace

void init()
{
    if (loaded)
        return;
    loaded = true;
    snprintf(message, sizeof(message), "%s",
             fieldText("Choose a city or refresh Zaragoza.", "Scegli una città o aggiorna Zaragoza.",
                       "Elige una ciudad o actualiza Zaragoza."));
    Cache cache{};
    Preferences p;
    if (p.begin("meshweather", true)) {
        if (p.getBytesLength("forecast") == sizeof(cache) && p.getBytes("forecast", &cache, sizeof(cache)) == sizeof(cache) &&
            cache.magic == 0x4d575831 && cache.checksum == checksum(cache.value) && validForecast(cache.value)) {
            current = cache.value;
            haveCurrent = true;
            snprintf(message, sizeof(message), "%s",
                     fieldText("Last saved forecast. Refresh via Wi-Fi.", "Ultimi dati salvati. Aggiorna via Wi-Fi.",
                               "Última previsión guardada. Actualiza por Wi-Fi."));
        }
        p.end();
    }
}

void service()
{
    if (!working || !done.load(std::memory_order_acquire))
        return;
    working = false;
    if (success && searchJob) {
        choicesCount = pendingCount;
        memcpy(choices, pendingChoices, sizeof(choices));
        snprintf(message, sizeof(message), "%s",
                 choicesCount ? fieldText("Choose a city from the results.", "Scegli una città dai risultati.",
                                          "Elige una ciudad de la lista.")
                              : fieldText("No cities found.", "Nessuna città trovata.", "No se encontraron ciudades."));
    } else if (success) {
        pending.fetched = static_cast<uint32_t>(time(nullptr));
        current = pending;
        haveCurrent = true;
        Cache cache{};
        cache.magic = 0x4d575831;
        cache.value = current;
        cache.checksum = checksum(cache.value);
        Preferences p;
        bool saved = false;
        if (p.begin("meshweather", false)) {
            saved = p.putBytes("forecast", &cache, sizeof(cache)) == sizeof(cache);
            p.end();
        }
        snprintf(message, sizeof(message),
                 saved ? fieldText("Updated %s", "Aggiornato %s", "Actualizado %s")
                       : fieldText("Updated, but could not save.", "Aggiornato, ma salvataggio fallito.",
                                   "Actualizado, pero no se pudo guardar."),
                 current.updated);
    } else {
        if (searchJob)
            choicesCount = 0;
        snprintf(message, sizeof(message),
                 fieldText("Weather unavailable (%d). Last forecast kept.", "Meteo non disponibile (%d). Ultimi dati conservati.",
                           "Tiempo no disponible (%d). Se conserva la última previsión."),
                 httpCode);
    }
    ++revision;
}

bool refresh(bool manual)
{
    init();
    service();
    if (!manual && lastAttempt && Throttle::isWithinTimespanMs(lastAttempt, 300000))
        return false;
    const time_t now = time(nullptr);
    if (!manual && haveCurrent && now >= current.fetched && now - current.fetched < 900)
        return false;
    requestedPlace = haveCurrent ? current.place : defaultPlace;
    return start(false);
}

bool search(const char *value)
{
    init();
    service();
    if (working || !value || strlen(value) < 2 || strlen(value) >= sizeof(query))
        return false;
    memcpy(query, value, strlen(value) + 1);
    choicesCount = 0;
    return start(true);
}

bool choose(unsigned index)
{
    service();
    if (working || index >= choicesCount)
        return false;
    requestedPlace = choices[index];
    return start(false);
}

bool busy()
{
    return working;
}
unsigned generation()
{
    return revision;
}
const char *status()
{
    return message;
}
const Forecast *forecast()
{
    return haveCurrent ? &current : nullptr;
}
unsigned placeCount()
{
    return choicesCount;
}
const Place *place(unsigned index)
{
    return index < choicesCount ? &choices[index] : nullptr;
}

const char *description(int code)
{
    if (code == 0)
        return fieldText("Clear", "Sereno", "Despejado");
    if (code == 1)
        return fieldText("Mostly clear", "Poco nuvoloso", "Poco nuboso");
    if (code == 2)
        return fieldText("Partly cloudy", "Parz. nuvoloso", "Parc. nuboso");
    if (code == 3)
        return fieldText("Overcast", "Coperto", "Cubierto");
    if (code == 45 || code == 48)
        return fieldText("Fog", "Nebbia", "Niebla");
    if (code >= 51 && code <= 57)
        return fieldText("Drizzle", "Pioviggine", "Llovizna");
    if (code >= 61 && code <= 67)
        return fieldText("Rain", "Pioggia", "Lluvia");
    if (code >= 71 && code <= 77)
        return fieldText("Snow", "Neve", "Nieve");
    if (code >= 80 && code <= 82)
        return fieldText("Showers", "Rovesci", "Chubascos");
    if (code == 85 || code == 86)
        return fieldText("Snow showers", "Rovesci di neve", "Chubascos de nieve");
    if (code >= 95 && code <= 99)
        return fieldText("Thunderstorms", "Temporali", "Tormentas");
    return fieldText("Unavailable", "Non disponibile", "No disponible");
}
} // namespace meshweather
