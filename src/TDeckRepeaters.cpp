#include "HearHamCertificate.h"
#include "configuration.h"
#include "graphics/common/SdCard.h"
#include "mesh/Throttle.h"
#include "util/RepeaterData.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <atomic>
#include <cJSON.h>
#include <cmath>
#include <cstring>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

extern "C" bool tdeck_wifi_connected(void);
extern "C" bool tdeck_network_job_acquire(int);
extern "C" void tdeck_network_job_release(int);
extern "C" void tdeck_tls_reserve_release(void);
extern "C" void tdeck_tls_reserve_take(void);
extern "C" bool tdeck_gps_position(int32_t *, int32_t *);

namespace
{
const char *dbPath = "/repeaters/hearham-v1.db";
const char *tmpPath = "/repeaters/hearham-v1.tmp";
const char *bakPath = "/repeaters/hearham-v1.bak";
struct Header {
    char magic[8];
    uint32_t version, recordSize, count, updated, checksum;
    char reserved[100];
};
static_assert(sizeof(Header) == 128, "Cache header format changed");
Header header = {};
FsFile output, scan;
QueueHandle_t queue = nullptr;
StaticQueue_t queueControl;
uint8_t *queueStore = nullptr;
std::atomic<bool> workerDone{false}, workerOK{false}, cancel{false};
std::atomic<unsigned> received{0}, skipped{0};
bool active = false, requested = false, loaded = false, haveReference = false, gpsReference = false;
bool lastGpsValid = false;
uint32_t written = 0, writeHash = 2166136261u, scanHash = 2166136261u, scanCount = 0;
uint32_t lastReferenceCheck = 0;
float refLat = 0, refLon = 0;
unsigned generation = 0, topCount = 0, workCount = 0;
struct Nearby {
    RepeaterRecord record;
    uint32_t metres;
};
Nearby *top = nullptr, *work = nullptr;
char status[160] = "Nessun archivio. Collega il Wi-Fi e aggiorna.";

uint32_t checksum(uint32_t hash, const void *data, size_t n)
{
    const uint8_t *p = (const uint8_t *)data;
    while (n--)
        hash = (hash ^ *p++) * 16777619u;
    return hash;
}

void text(cJSON *item, const char *name, char *out, size_t cap)
{
    cJSON *v = cJSON_GetObjectItemCaseSensitive(item, name);
    const char *s = cJSON_IsString(v) ? v->valuestring : "";
    size_t n = strlen(s);
    if (n >= cap) {
        n = cap - 1;
        while (n && ((unsigned char)s[n] & 0xc0) == 0x80)
            n--;
    }
    memcpy(out, s, n);
    out[n] = 0;
}

double number(cJSON *item, const char *name, double missing)
{
    cJSON *v = cJSON_GetObjectItemCaseSensitive(item, name);
    if (cJSON_IsNumber(v))
        return v->valuedouble;
    if (cJSON_IsString(v)) {
        char *end;
        double result = strtod(v->valuestring, &end);
        if (end != v->valuestring && !*end)
            return result;
    }
    return missing;
}

bool recordFromJSON(const char *json, RepeaterRecord &r)
{
    cJSON *item = cJSON_Parse(json);
    if (!item)
        return false;
    double la = number(item, "latitude", 999), lo = number(item, "longitude", 999);
    double hz = number(item, "frequency", 0), offset = number(item, "offset", 0);
    bool ok = la >= -90 && la <= 90 && lo >= -180 && lo <= 180 && hz >= 1000000 && hz <= 4294967295.0 &&
              offset >= -2147483648.0 && offset <= 2147483647.0;
    memset(&r, 0, sizeof(r));
    text(item, "callsign", r.callsign, sizeof(r.callsign));
    ok = ok && r.callsign[0];
    if (ok) {
        double id = number(item, "id", 0);
        r.id = id >= 0 && id <= 4294967295.0 ? (uint32_t)id : 0;
        r.frequencyHz = (uint32_t)hz;
        r.offsetHz = (int32_t)offset;
        r.latitudeE7 = (int32_t)llround(la * 1e7);
        r.longitudeE7 = (int32_t)llround(lo * 1e7);
        text(item, "city", r.city, sizeof(r.city));
        text(item, "mode", r.mode, sizeof(r.mode));
        text(item, "encode", r.encode, sizeof(r.encode));
        text(item, "decode", r.decode, sizeof(r.decode));
        text(item, "restriction", r.restriction, sizeof(r.restriction));
        cJSON *op = cJSON_GetObjectItemCaseSensitive(item, "operational");
        r.operational = cJSON_IsTrue(op) || (cJSON_IsNumber(op) && op->valueint != 0);
    }
    cJSON_Delete(item);
    return ok;
}

class RepeaterStream : public Stream
{
    char *object;
    size_t used = 0, bytes = 0;
    int depth = 0;
    bool quoted = false, escaped = false, overflow = false;

  public:
    bool started = false, ended = false, valid = true;
    RepeaterStream()
    {
        object = (char *)heap_caps_malloc(8192, MALLOC_CAP_SPIRAM);
        valid = object != nullptr;
    }
    ~RepeaterStream() { free(object); }
    size_t write(uint8_t c) override { return write(&c, 1); }
    size_t write(const uint8_t *data, size_t n) override
    {
        if (!valid || cancel.load() || bytes + n > 20 * 1024 * 1024)
            return 0;
        bytes += n;
        for (size_t i = 0; i < n; i++) {
            char c = data[i];
            if (!started) {
                if (isspace((unsigned char)c))
                    continue;
                if (c != '[') {
                    valid = false;
                    return 0;
                }
                started = true;
                continue;
            }
            if (!depth) {
                if (isspace((unsigned char)c) || (!ended && c == ','))
                    continue;
                if (!ended && c == ']') {
                    ended = true;
                    continue;
                }
                if (ended || c != '{') {
                    valid = false;
                    return 0;
                }
                depth = 1;
                used = 0;
                overflow = false;
                quoted = escaped = false;
            } else if (quoted) {
                if (escaped)
                    escaped = false;
                else if (c == '\\')
                    escaped = true;
                else if (c == '"')
                    quoted = false;
            } else {
                if (c == '"')
                    quoted = true;
                else if (c == '{' || c == '[') {
                    if (++depth > 16) {
                        valid = false;
                        return 0;
                    }
                } else if (c == '}' || c == ']')
                    depth--;
            }
            if (used < 8191)
                object[used++] = c;
            else
                overflow = true;
            if (!depth) {
                object[used] = 0;
                RepeaterRecord record;
                if (!overflow && recordFromJSON(object, record)) {
                    while (xQueueSend(queue, &record, pdMS_TO_TICKS(50)) != pdTRUE)
                        if (cancel.load())
                            return 0;
                    received.fetch_add(1);
                } else
                    skipped.fetch_add(1);
            }
        }
        return n;
    }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}
};

void download(void *)
{
    bool ok = false;
    {
        tdeck_tls_reserve_release();
        WiFiClientSecure client;
        client.setCACert(kHearHamRootCA);
        client.setHandshakeTimeout(15);
        HTTPClient http;
        http.setConnectTimeout(8000);
        http.setTimeout(15000);
        http.setReuse(false);
        http.setUserAgent("TDeck-Field/0.1 HearHam offline client");
        if (http.begin(client, "https://hearham.com/api/repeaters/v1") && http.GET() == 200) {
            RepeaterStream sink;
            int got = sink.valid ? http.writeToStream(&sink) : -1;
            ok = got > 0 && sink.valid && sink.started && sink.ended && received.load() > 100 && !cancel.load();
        }
        http.end();
    } // Free TLS buffers before publishing completion.
    workerOK.store(ok, std::memory_order_release);
    workerDone.store(true, std::memory_order_release);
    vTaskDelete(nullptr);
}

bool loadHeader()
{
    if (!SDFs.exists(dbPath) && SDFs.exists(bakPath))
        SDFs.rename(bakPath, dbPath);
    FsFile file = SDFs.open(dbPath, O_RDONLY);
    Header h;
    bool ok = file && file.read(&h, sizeof(h)) == sizeof(h) && !memcmp(h.magic, "RPTDB01", 8) && h.version == 1 &&
              h.recordSize == sizeof(RepeaterRecord) && h.count > 0 && h.count <= 100000 &&
              file.fileSize() == sizeof(Header) + (uint64_t)h.count * sizeof(RepeaterRecord);
    if (file)
        file.close();
    if (ok)
        header = h;
    return ok;
}

void startScan()
{
    if (scan)
        scan.close();
    if (!haveReference || !header.count)
        return;
    scan = SDFs.open(dbPath, O_RDONLY);
    if (scan && scan.seekSet(sizeof(Header))) {
        scanCount = 0;
        scanHash = 2166136261u;
        workCount = 0;
    } else if (scan)
        scan.close();
}

uint32_t distance(float la, float lo)
{
    constexpr float rad = 0.017453292519943295f;
    float a = sinf((la - refLat) * rad / 2), b = sinf((lo - refLon) * rad / 2);
    float h = a * a + cosf(la * rad) * cosf(refLat * rad) * b * b;
    h = h < 0 ? 0 : h > 1 ? 1 : h;
    return (uint32_t)(12742000.0f * atan2f(sqrtf(h), sqrtf(1 - h)));
}

void scanStep()
{
    if (!scan || !work)
        return;
    uint32_t start = millis();
    for (int i = 0; i < 160 && Throttle::isWithinTimespanMs(start, 8) && scanCount < header.count; i++) {
        RepeaterRecord record;
        if (scan.read(&record, sizeof(record)) != sizeof(record)) {
            scanCount = header.count;
            scanHash ^= 1;
            break;
        }
        scanHash = checksum(scanHash, &record, sizeof(record));
        scanCount++;
        record.callsign[sizeof(record.callsign) - 1] = 0;
        record.city[sizeof(record.city) - 1] = 0;
        record.mode[sizeof(record.mode) - 1] = 0;
        record.encode[sizeof(record.encode) - 1] = 0;
        record.decode[sizeof(record.decode) - 1] = 0;
        record.restriction[sizeof(record.restriction) - 1] = 0;
        uint32_t metres = distance(record.latitudeE7 / 1e7f, record.longitudeE7 / 1e7f);
        unsigned at = 0;
        while (at < workCount && work[at].metres <= metres)
            at++;
        if (at >= 40)
            continue;
        unsigned last = workCount < 40 ? workCount++ : 39;
        for (unsigned j = last; j > at; j--)
            work[j] = work[j - 1];
        work[at] = {record, metres};
    }
    if (scanCount == header.count) {
        scan.close();
        if (scanHash == header.checksum) {
            Nearby *swap = top;
            top = work;
            work = swap;
            topCount = workCount;
            generation++;
        } else {
            header.count = 0;
            topCount = 0;
            generation++;
            snprintf(status, sizeof(status), "Archivio danneggiato. Aggiorna via Wi-Fi.");
        }
    }
}

void beginUpdate()
{
    if (active)
        return;
    if (!tdeck_wifi_connected()) {
        requested = false;
        snprintf(status, sizeof(status), "Wi-Fi non collegato. Uso l'archivio offline.");
        return;
    }
    if (!tdeck_network_job_acquire(2)) {
        snprintf(status, sizeof(status), "Rete occupata: aggiornamento in attesa...");
        return;
    }
    if (!queueStore)
        queueStore = (uint8_t *)heap_caps_malloc(24 * sizeof(RepeaterRecord), MALLOC_CAP_SPIRAM);
    if (!queueStore) {
        tdeck_network_job_release(2);
        return;
    }
    if (!queue)
        queue = xQueueCreateStatic(24, sizeof(RepeaterRecord), queueStore, &queueControl);
    xQueueReset(queue);
    SDFs.mkdir("/repeaters");
    output = SDFs.open(tmpPath, O_WRONLY | O_CREAT | O_TRUNC);
    Header empty = {};
    if (!output || output.write(&empty, sizeof(empty)) != sizeof(empty)) {
        if (output)
            output.close();
        snprintf(status, sizeof(status), "MicroSD non scrivibile. Archivio precedente conservato.");
        requested = false;
        tdeck_network_job_release(2);
        return;
    }
    received.store(0);
    skipped.store(0);
    workerDone.store(false);
    workerOK.store(false);
    cancel.store(false);
    written = 0;
    writeHash = 2166136261u;
    active = true;
    requested = false;
    snprintf(status, sizeof(status), "Aggiornamento HearHam...");
    if (xTaskCreate(download, "repeaters", 8192, nullptr, 1, nullptr) != pdPASS) {
        output.close();
        active = false;
        snprintf(status, sizeof(status), "Memoria insufficiente. Riprova.");
        tdeck_network_job_release(2);
    }
}

void finishUpdate()
{
    bool ok = workerOK.load(std::memory_order_acquire) && !cancel.load() && written == received.load();
    Header h = {};
    memcpy(h.magic, "RPTDB01", 8);
    h.version = 1;
    h.recordSize = sizeof(RepeaterRecord);
    h.count = written;
    h.updated = (uint32_t)time(nullptr);
    h.checksum = writeHash;
    if (ok)
        ok = output.seekSet(0) && output.write(&h, sizeof(h)) == sizeof(h) && output.sync();
    output.close();
    if (ok) {
        if (scan)
            scan.close();
        if (SDFs.exists(bakPath))
            SDFs.remove(bakPath);
        bool had = SDFs.exists(dbPath);
        ok = !had || SDFs.rename(dbPath, bakPath);
        if (ok) {
            ok = SDFs.rename(tmpPath, dbPath);
            if (!ok && had)
                SDFs.rename(bakPath, dbPath);
        }
        if (ok) {
            SDFs.remove(bakPath);
            header = h;
            startScan();
            snprintf(status, sizeof(status), "%lu ripetitori salvati. %u voci escluse.", (unsigned long)h.count, skipped.load());
        }
    }
    if (!ok) {
        SDFs.remove(tmpPath);
        snprintf(status, sizeof(status), "Aggiornamento non riuscito. Archivio precedente conservato.");
    }
    active = false;
    tdeck_tls_reserve_take();
    tdeck_network_job_release(2);
}
} // namespace

extern "C" void tdeck_repeaters_open(void)
{
    if (!loaded) {
        top = (Nearby *)heap_caps_calloc(40, sizeof(Nearby), MALLOC_CAP_SPIRAM);
        work = (Nearby *)heap_caps_calloc(40, sizeof(Nearby), MALLOC_CAP_SPIRAM);
        loaded = top && work;
        if (!loaded) {
            free(top);
            free(work);
            top = work = nullptr;
            snprintf(status, sizeof(status), "Memoria insufficiente.");
            return;
        }
        if (loadHeader()) {
            snprintf(status, sizeof(status), "%lu ripetitori offline. Fonte: HearHam.", (unsigned long)header.count);
            startScan();
        }
    }
    if (tdeck_wifi_connected() && !active)
        requested = true;
}
extern "C" void tdeck_repeaters_request_update(void)
{
    if (!tdeck_wifi_connected()) {
        snprintf(status, sizeof(status), "Wi-Fi non collegato. Uso l'archivio offline.");
        return;
    }
    if (!active)
        requested = true;
}
extern "C" void tdeck_repeaters_use_zaragoza(void)
{
    if (!loaded)
        return;
    refLat = 41.65f;
    refLon = -0.89f;
    haveReference = true;
    gpsReference = false;
    startScan();
}
extern "C" void tdeck_repeaters_ui_service(void)
{
    if (!loaded)
        return;
    if (requested)
        beginUpdate();
    if (active) {
        RepeaterRecord record;
        for (int i = 0; i < 40 && xQueueReceive(queue, &record, 0) == pdTRUE; i++) {
            if (!cancel.load()) {
                if (output.write(&record, sizeof(record)) != sizeof(record))
                    cancel.store(true);
                else {
                    written++;
                    writeHash = checksum(writeHash, &record, sizeof(record));
                }
            }
        }
        snprintf(status, sizeof(status), "Importati: %lu ripetitori", (unsigned long)received.load());
        if (workerDone.load(std::memory_order_acquire) && !uxQueueMessagesWaiting(queue))
            finishUpdate();
    }
    if (!Throttle::isWithinTimespanMs(lastReferenceCheck, 10000) || !lastReferenceCheck) {
        lastReferenceCheck = millis();
        int32_t la, lo;
        lastGpsValid = tdeck_gps_position(&la, &lo);
        if (lastGpsValid && (!gpsReference || !haveReference || distance(la / 1e7f, lo / 1e7f) > 250)) {
            refLat = la / 1e7f;
            refLon = lo / 1e7f;
            haveReference = gpsReference = true;
            startScan();
        }
    }
    scanStep();
}
extern "C" unsigned tdeck_repeaters_generation(void)
{
    return generation;
}
extern "C" unsigned tdeck_repeaters_count(void)
{
    return header.count;
}
extern "C" unsigned tdeck_repeaters_near_count(void)
{
    return topCount;
}
extern "C" const RepeaterRecord *tdeck_repeaters_record(unsigned i)
{
    return i < topCount ? &top[i].record : nullptr;
}
extern "C" uint32_t tdeck_repeaters_distance(unsigned i)
{
    return i < topCount ? top[i].metres : 0;
}
extern "C" const char *tdeck_repeaters_status(void)
{
    return status;
}
extern "C" const char *tdeck_repeaters_reference(void)
{
    return !haveReference  ? "GPS assente. Scegli Zaragoza."
           : !gpsReference ? "Zaragoza (riferimento manuale)"
           : lastGpsValid  ? "Distanze dalla posizione GPS"
                           : "Distanze dall'ultimo fix GPS";
}
extern "C" bool tdeck_repeaters_busy(void)
{
    return active || requested;
}
