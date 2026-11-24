#include "FieldUpdateKey.h"
#include "FieldUpdateSelfTest.h"
#include "GitHubCertificate.h"
#include "configuration.h"
#include "mesh/Throttle.h"
#include "util/FieldUpdate.h"
#include "util/HttpBodyBuffer.h"
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <atomic>
#include <cJSON.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <mbedtls/base64.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>

extern "C" bool tdeck_wifi_connected(void);
extern "C" bool tdeck_network_job_acquire(int);
extern "C" void tdeck_network_job_release(int);
extern "C" void tdeck_tls_reserve_release(void);
extern "C" void tdeck_tls_reserve_take(void);

namespace
{
const char *manifestURL = "https://raw.githubusercontent.com/Aleph72/t-ui-firmware/field/ota/stable.json";
std::atomic<bool> requested{false}, working{false}, rebootReady{false}, automatic{true};
std::atomic<int> autoPending{-1};
std::atomic<unsigned> percent{0};
portMUX_TYPE statusMux = portMUX_INITIALIZER_UNLOCKED;
char message[144] = "Controlla aggiornamenti del firmware Field.";
bool initialized = false;
uint32_t lastCheck = 0, rebootStarted = 0, lastAutoTry = 0;

void say(const char *text)
{
    portENTER_CRITICAL(&statusMux);
    snprintf(message, sizeof(message), "%s", text);
    portEXIT_CRITICAL(&statusMux);
}

bool getBytes(WiFiClientSecure &client, const char *url, uint8_t *out, size_t cap, size_t &n, int &code)
{
    HTTPClient http;
    http.setConnectTimeout(8000);
    http.setTimeout(15000);
    http.setReuse(false);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setRedirectLimit(4);
    if (!http.begin(client, url))
        return false;
    code = http.GET();
    HttpBodyBuffer body(out, cap);
    int got = code == 200 ? http.writeToStream(&body) : -1;
    http.end();
    n = body.size();
    return got > 0 && !body.overflowed() && (size_t)got == n;
}

bool verifySignature(const uint8_t *data, size_t size, const uint8_t *signature, size_t signatureSize)
{
    uint8_t digest[32];
    if (mbedtls_sha256(data, size, digest, 0))
        return false;
    mbedtls_pk_context key;
    mbedtls_pk_init(&key);
    bool ok = mbedtls_pk_parse_public_key(&key, (const uint8_t *)kFieldUpdatePublicKey, strlen(kFieldUpdatePublicKey) + 1) == 0;
    if (ok)
        ok = mbedtls_pk_verify(&key, MBEDTLS_MD_SHA256, digest, sizeof(digest), signature, signatureSize) == 0;
    mbedtls_pk_free(&key);
    return ok;
}

struct Manifest {
    uint32_t version, size;
    uint8_t sha[32];
    char url[320];
};
bool field(cJSON *root, const char *key, const char *value)
{
    cJSON *v = cJSON_GetObjectItemCaseSensitive(root, key);
    return cJSON_IsString(v) && !strcmp(v->valuestring, value);
}
bool u32(cJSON *root, const char *name, uint32_t &out)
{
    cJSON *v = cJSON_GetObjectItemCaseSensitive(root, name);
    if (!cJSON_IsNumber(v) || !(v->valuedouble >= 0 && v->valuedouble <= 4294967295.0) || floor(v->valuedouble) != v->valuedouble)
        return false;
    out = (uint32_t)v->valuedouble;
    return true;
}
int hex(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}
bool parseManifest(const char *json, Manifest &m)
{
    cJSON *root = cJSON_Parse(json);
    if (!root)
        return false;
    uint32_t schema = 0;
    bool ok = u32(root, "schema_version", schema) && schema == 1 && field(root, "board", "t-deck-plus-16mb") &&
              field(root, "chip", "esp32s3") && field(root, "image_type", "app") && u32(root, "version_code", m.version) &&
              u32(root, "size", m.size);
    cJSON *url = cJSON_GetObjectItemCaseSensitive(root, "url"), *hash = cJSON_GetObjectItemCaseSensitive(root, "sha256");
    const char *prefix = "https://github.com/Aleph72/t-ui-firmware/releases/download/";
    if (!cJSON_IsString(url) || strlen(url->valuestring) >= sizeof(m.url) || strncmp(url->valuestring, prefix, strlen(prefix)) ||
        !strrchr(url->valuestring, '/') || strcmp(strrchr(url->valuestring, '/'), "/firmware-t-deck-plus.bin") ||
        !cJSON_IsString(hash) || strlen(hash->valuestring) != 64)
        ok = false;
    if (ok) {
        snprintf(m.url, sizeof(m.url), "%s", url->valuestring);
        if (strstr(m.url, ".."))
            ok = false;
        for (const char *p = m.url + strlen(prefix); *p; p++)
            if (!(isalnum((unsigned char)*p) || *p == '.' || *p == '_' || *p == '/' || *p == '-'))
                ok = false;
        for (const char *p = m.url; *p; p++)
            if ((unsigned char)*p <= 32 || *p == '?' || *p == '#')
                ok = false;
        for (int i = 0; i < 32; i++) {
            int a = hex(hash->valuestring[i * 2]), b = hex(hash->valuestring[i * 2 + 1]);
            if (a < 0 || b < 0) {
                ok = false;
                break;
            }
            m.sha[i] = (a << 4) | b;
        }
    }
    cJSON_Delete(root);
    return ok;
}

class FirmwareStream : public Stream
{
    esp_ota_handle_t handle;
    mbedtls_sha256_context hash;
    uint32_t limit, received = 0;
    uint8_t imageHeader[36] = {};
    size_t headerSize = 0;

  public:
    bool valid = true;
    FirmwareStream(esp_ota_handle_t h, uint32_t n) : handle(h), limit(n)
    {
        mbedtls_sha256_init(&hash);
        valid = mbedtls_sha256_starts(&hash, 0) == 0;
    }
    ~FirmwareStream() { mbedtls_sha256_free(&hash); }
    size_t write(uint8_t value) override { return write(&value, 1); }
    size_t write(const uint8_t *data, size_t n) override
    {
        size_t copy = n < sizeof(imageHeader) - headerSize ? n : sizeof(imageHeader) - headerSize;
        memcpy(imageHeader + headerSize, data, copy);
        headerSize += copy;
        // ESP application descriptor starts at byte 32; reject bootloader/factory images.
        if (headerSize == sizeof(imageHeader) && (imageHeader[0] != 0xe9 || imageHeader[32] != 0x32 || imageHeader[33] != 0x54 ||
                                                  imageHeader[34] != 0xcd || imageHeader[35] != 0xab)) {
            valid = false;
            return 0;
        }
        if (!valid || n > limit - received || esp_ota_write(handle, data, n) != ESP_OK || mbedtls_sha256_update(&hash, data, n)) {
            valid = false;
            return 0;
        }
        received += n;
        percent.store((unsigned)((uint64_t)received * 100 / limit));
        taskYIELD();
        return n;
    }
    bool finish(uint8_t digest[32]) { return valid && received == limit && mbedtls_sha256_finish(&hash, digest) == 0; }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}
};

bool update()
{
    if (ESP.getFlashChipSize() != 16 * 1024 * 1024) {
        say("Hardware non compatibile con questo canale OTA.");
        return false;
    }
    tdeck_tls_reserve_release();
    WiFiClientSecure client;
    client.setCACert(kGitHubRootCA);
    client.setHandshakeTimeout(15);
    uint8_t *json = (uint8_t *)heap_caps_malloc(2049, MALLOC_CAP_SPIRAM);
    uint8_t signature[128];
    size_t jsonSize = 0, signatureSize = 0;
    int code = 0;
    if (!json) {
        say("Memoria insufficiente.");
        return false;
    }
    bool ok = getBytes(client, manifestURL, json, 2048, jsonSize, code);
    if (!ok) {
        free(json);
        say(code == 404 ? "Canale OTA non ancora pubblicato su GitHub." : "Controllo non riuscito. Riprova con Internet.");
        return false;
    }
    json[jsonSize] = 0;
    // Payload and detached signature travel together so CDN caching cannot mix releases.
    cJSON *envelope = cJSON_Parse((char *)json);
    cJSON *payload = cJSON_GetObjectItemCaseSensitive(envelope, "payload");
    cJSON *sig = cJSON_GetObjectItemCaseSensitive(envelope, "signature");
    ok = cJSON_IsString(payload) && cJSON_IsString(sig);
    if (ok) {
        ok = mbedtls_base64_decode(signature, sizeof(signature), &signatureSize, (const uint8_t *)sig->valuestring,
                                   strlen(sig->valuestring)) == 0 &&
             mbedtls_base64_decode(json, 2048, &jsonSize, (const uint8_t *)payload->valuestring, strlen(payload->valuestring)) ==
                 0;
    }
    cJSON_Delete(envelope);
    json[jsonSize <= 2048 ? jsonSize : 2048] = 0;
    ok = ok && verifySignature(json, jsonSize, signature, signatureSize);
    Manifest manifest = {};
    if (!ok || !parseManifest((char *)json, manifest)) {
        free(json);
        say("Aggiornamento rifiutato: firma o formato non validi.");
        return false;
    }
    free(json);
    if (manifest.version <= kFieldVersionCode) {
        say("Il firmware e' gia' aggiornato.");
        return false;
    }
    const esp_partition_t *partition = esp_ota_get_next_update_partition(nullptr);
    if (!partition || partition->address == esp_ota_get_running_partition()->address || manifest.size < 1024 ||
        manifest.size > partition->size) {
        say("Aggiornamento rifiutato: dimensione o partizione errata.");
        return false;
    }
    say("Scarico e verifico il nuovo firmware...");
    HTTPClient http;
    http.setConnectTimeout(8000);
    http.setTimeout(15000);
    http.setReuse(false);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setRedirectLimit(4);
    if (!http.begin(client, manifest.url) || http.GET() != 200) {
        http.end();
        say("Download non riuscito. Firmware attuale conservato.");
        return false;
    }
    int size = http.getSize();
    if (size >= 0 && (uint32_t)size != manifest.size) {
        http.end();
        say("Dimensione del download non valida.");
        return false;
    }
    esp_ota_handle_t handle;
    if (esp_ota_begin(partition, manifest.size, &handle) != ESP_OK) {
        http.end();
        say("Impossibile preparare la partizione OTA.");
        return false;
    }
    uint8_t digest[32];
    {
        FirmwareStream sink(handle, manifest.size);
        int got = http.writeToStream(&sink);
        ok = got > 0 && sink.finish(digest) && !memcmp(digest, manifest.sha, sizeof(digest));
    }
    http.end();
    if (!ok) {
        esp_ota_abort(handle);
        say("Download incompleto o SHA-256 errato. Nessuna installazione.");
        return false;
    }
    if (esp_ota_end(handle) != ESP_OK || esp_ota_set_boot_partition(partition) != ESP_OK) {
        say("Immagine firmware non valida. Nessun riavvio.");
        return false;
    }
    say("Aggiornamento verificato. Riavvio tra pochi secondi.");
    return true;
}

void task(void *)
{
    bool installed = update();
    tdeck_tls_reserve_take();
    if (installed)
        rebootReady.store(true, std::memory_order_release);
    else
        tdeck_network_job_release(3);
    working.store(false, std::memory_order_release);
    vTaskDelete(nullptr);
}
} // namespace

extern "C" bool tdeck_ota_selftest(void)
{
    uint8_t altered[sizeof(kUpdateTestSignature)];
    memcpy(altered, kUpdateTestSignature, sizeof(altered));
    altered[sizeof(altered) - 1] ^= 1;
    return verifySignature(kUpdateTestData, sizeof(kUpdateTestData) - 1, kUpdateTestSignature, sizeof(kUpdateTestSignature)) &&
           !verifySignature(kUpdateTestData, sizeof(kUpdateTestData) - 2, kUpdateTestSignature, sizeof(kUpdateTestSignature)) &&
           !verifySignature(kUpdateTestData, sizeof(kUpdateTestData) - 1, altered, sizeof(altered));
}

extern "C" void tdeck_ota_request(void)
{
    if (!working.load() && !rebootReady.load())
        requested.store(true);
}
extern "C" void tdeck_ota_set_auto(bool enabled)
{
    autoPending.store(enabled ? 1 : 0);
}
extern "C" bool tdeck_ota_get_auto(void)
{
    return automatic.load();
}
extern "C" bool tdeck_ota_busy(void)
{
    return working.load() || rebootReady.load();
}
extern "C" unsigned tdeck_ota_progress(void)
{
    return percent.load();
}
extern "C" void tdeck_ota_status(char *out, size_t cap)
{
    portENTER_CRITICAL(&statusMux);
    snprintf(out, cap, "%s", message);
    portEXIT_CRITICAL(&statusMux);
}
extern "C" void tdeck_ota_service(void)
{
    static bool bootChecked = false;
    if (!bootChecked && !Throttle::isWithinTimespanMs(0, 60000)) {
        esp_ota_img_states_t bootState;
        if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &bootState) == ESP_OK &&
            bootState == ESP_OTA_IMG_PENDING_VERIFY)
            esp_ota_mark_app_valid_cancel_rollback();
        bootChecked = true;
    }
    if (!initialized) {
        Preferences prefs;
        if (prefs.begin("tui-updates", true)) {
            automatic.store(prefs.getBool("daily", true));
            lastCheck = prefs.getUInt("last", 0);
            prefs.end();
        }
        initialized = true;
    }
    int want = autoPending.exchange(-1);
    if (want >= 0) {
        Preferences prefs;
        if (prefs.begin("tui-updates", false)) {
            prefs.putBool("daily", want != 0);
            prefs.end();
            automatic.store(want != 0);
        }
    }
    if (rebootReady.load(std::memory_order_acquire)) {
        if (!rebootStarted)
            rebootStarted = millis();
        if (!Throttle::isWithinTimespanMs(rebootStarted, 4000))
            ESP.restart();
        return;
    }
    if (working.load())
        return;
    uint32_t now = (uint32_t)time(nullptr);
    bool due = automatic.load() && now > 1700000000u && (!lastCheck || (now >= lastCheck && now - lastCheck >= 86400));
    bool manual = requested.load();
    if (!manual && !(due && tdeck_field_is_idle() && !Throttle::isWithinTimespanMs(lastAutoTry, 60000)))
        return;
    lastAutoTry = millis();
    if (!tdeck_wifi_connected()) {
        if (manual) {
            requested.store(false);
            say("Wi-Fi non collegato. Collega Internet e riprova.");
        }
        return;
    }
    if (!tdeck_network_job_acquire(3)) {
        if (manual)
            say("Rete occupata: il controllo partira' appena libera.");
        return;
    }
    requested.store(false);
    working.store(true);
    percent.store(0);
    say("Controllo aggiornamenti firmati su GitHub...");
    if (xTaskCreate(task, "field-ota", 8192, nullptr, 1, nullptr) != pdPASS) {
        working.store(false);
        tdeck_network_job_release(3);
        say("Memoria insufficiente. Riprova.");
        return;
    }
    lastCheck = now;
    Preferences prefs;
    if (prefs.begin("tui-updates", false)) {
        prefs.putUInt("last", lastCheck);
        prefs.end();
    }
}
