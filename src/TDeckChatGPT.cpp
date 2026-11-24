#include "HearHamCertificate.h"
#include "OpenAICertificate.h"
#include "configuration.h"
#include "mesh/Throttle.h"
#include "util/ChatGPTPlan.h"
#include "util/ChatGPTPlanData.h"
#include "util/HttpBodyBuffer.h"
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <mbedtls/base64.h>
#include <mbedtls/platform_util.h>
#include <mbedtls/sha256.h>

extern "C" bool tdeck_wifi_connected(void);
extern "C" bool tdeck_network_job_acquire(int);
extern "C" void tdeck_network_job_release(int);
extern "C" void tdeck_tls_reserve_release(void);
extern "C" void tdeck_tls_reserve_take(void);

namespace tdeckchatgpt
{
namespace
{
constexpr size_t kCredentialMax = 12288;
constexpr size_t kReplyMax = 2401;
std::atomic<int> stateValue{0}, configResult{0}, importResult{0}, persistResult{1};
std::atomic<bool> invalidSession{false};
std::atomic<bool> loaded{false}, ready{false}, workerActive{false}, pendingAsk{false}, logoutRequested{false};
std::atomic<char *> pendingSession{nullptr};
char *session = nullptr, *incoming = nullptr, *answer = nullptr, *previousPrompt = nullptr, *currentPrompt = nullptr;
size_t incomingSize = 0;
bool importing = false;
char prompt[768] = {}, host[64] = {}, model[96] = {}, account[96] = {};
char message[160] = "Collega il tuo account ChatGPT dal Mac via USB.";
char *pendingAskJSON = nullptr;
portMUX_TYPE messageMux = portMUX_INITIALIZER_UNLOCKED;

void wipeFree(char *p)
{
    if (p) {
        mbedtls_platform_zeroize(p, strlen(p));
        free(p);
    }
}
char *copyPSRAM(const char *s)
{
    size_t n = strlen(s);
    char *out = (char *)heap_caps_malloc(n + 1, MALLOC_CAP_SPIRAM);
    if (out)
        memcpy(out, s, n + 1);
    return out;
}
void say(const char *text)
{
    portENTER_CRITICAL(&messageMux);
    snprintf(message, sizeof(message), "%s", text);
    portEXIT_CRITICAL(&messageMux);
}
void fail(const char *text)
{
    say(text);
    stateValue.store(3, std::memory_order_release);
}
uint32_t integer(const cJSON *root, const char *key)
{
    return planEpoch(root, key);
}
void updateLabels(const cJSON *root)
{
    const char *selected = planString(root, "model", sizeof(model) - 1);
    const char *email = planString(root, "email", sizeof(account) - 1);
    snprintf(model, sizeof(model), "%s", selected ? selected : "");
    snprintf(account, sizeof(account), "%s", email ? email : "Account ChatGPT");
}
bool persistFromWorker(cJSON *root)
{
    char *json = cJSON_PrintUnformatted(root);
    char *copy = json && strlen(json) <= kCredentialMax ? copyPSRAM(json) : nullptr;
    if (json) {
        mbedtls_platform_zeroize(json, strlen(json));
        cJSON_free(json);
    }
    if (!copy)
        return false;
    persistResult.store(0);
    pendingSession.store(copy, std::memory_order_release);
    uint32_t started = millis();
    while (!persistResult.load(std::memory_order_acquire) && Throttle::isWithinTimespanMs(started, 10000))
        vTaskDelay(pdMS_TO_TICKS(10));
    return persistResult.load() == 1;
}
bool responseBody(HTTPClient &http, char *out, size_t cap)
{
    HttpBodyBuffer sink((uint8_t *)out, cap - 1);
    int got = http.writeToStream(&sink);
    if (got <= 0 || sink.overflowed() || (size_t)got != sink.size())
        return false;
    out[got] = 0;
    return true;
}
PlanError errorBody(HTTPClient &http)
{
    char *body = (char *)heap_caps_calloc(2049, 1, MALLOC_CAP_SPIRAM);
    if (!body)
        return PlanError::OTHER;
    cJSON *root = responseBody(http, body, 2049) ? cJSON_Parse(body) : nullptr;
    const cJSON *error = cJSON_GetObjectItemCaseSensitive(root, "error");
    PlanError result = planErrorCode(planString(error, "code", 128));
    cJSON_Delete(root);
    wipeFree(body);
    return result;
}
void reportError(PlanError code, int http)
{
    if (code == PlanError::LIMIT || http == 429)
        fail("Limite del piano o dell'app raggiunto. Controlla chatgpt.com/settings/usage.");
    else if (code == PlanError::INELIGIBLE)
        fail("Questo account o workspace non e' abilitato per l'uso del piano in questa app.");
    else if (code == PlanError::AUTH || http == 401)
        fail("Accesso ChatGPT non autorizzato. Ricollega l'account dal Mac.");
    else if (code == PlanError::UNSUPPORTED || http == 400 || http == 404)
        fail("Modello o funzione non disponibili per questo account. Ricollega dal Mac.");
    else if (code == PlanError::UNAVAILABLE || http == 503)
        fail("ChatGPT temporaneamente non disponibile. Riprova piu' tardi.");
    else if (http == 403)
        fail("Accesso limitato dalla policy dell'account. Verifica i permessi in ChatGPT.");
    else
        fail("Richiesta non completata. Verifica Wi-Fi, Internet e ora.");
}
String encodeForm(const char *text)
{
    String out;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '-' || *p == '.' ||
            *p == '_' || *p == '~')
            out += (char)*p;
        else {
            char encoded[4];
            snprintf(encoded, sizeof(encoded), "%%%02X", *p);
            out += encoded;
        }
    }
    return out;
}
void replaceString(cJSON *root, const char *name, const char *value)
{
    cJSON_DeleteItemFromObjectCaseSensitive(root, name);
    cJSON_AddStringToObject(root, name, value);
}
void replaceNumber(cJSON *root, const char *name, uint32_t value)
{
    cJSON_DeleteItemFromObjectCaseSensitive(root, name);
    cJSON_AddNumberToObject(root, name, value);
}
bool refresh(cJSON *credentials)
{
    uint32_t now = (uint32_t)time(nullptr), expiry = integer(credentials, "expires_at");
    if (now < 1700000000u) {
        fail("Ora non sincronizzata. Attendi il collegamento Wi-Fi.");
        return false;
    }
    if (expiry > now + 60)
        return true;
    uint32_t earliest = integer(credentials, "earliest_refresh_at");
    if (earliest > now) {
        fail("Rinnovo ChatGPT non ancora disponibile. Riprova tra poco.");
        return false;
    }
    say("Rinnovo della sessione ChatGPT...");
    const char *client = planString(credentials, "client_id", 128);
    const char *token = planString(credentials, "refresh_token", 4096);
    if (!client || !token)
        return false;
    String form = "grant_type=refresh_token&client_id=" + encodeForm(client) + "&refresh_token=" + encodeForm(token) +
                  "&resource=https%3A%2F%2Fapi.openai.com%2Fv1";
    WiFiClientSecure tls;
    tls.setCACert(kHearHamRootCA);
    tls.setHandshakeTimeout(15);
    HTTPClient http;
    http.setConnectTimeout(8000);
    http.setTimeout(20000);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    if (!http.begin(tls, "https://auth.openai.com/api/accounts/oauth/token"))
        return false;
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    int code = http.POST((uint8_t *)form.c_str(), form.length());
    mbedtls_platform_zeroize((void *)form.c_str(), form.length());
    char *body = (char *)heap_caps_calloc(kCredentialMax + 1, 1, MALLOC_CAP_SPIRAM);
    bool got = body && responseBody(http, body, kCredentialMax + 1);
    http.end();
    tls.stop();
    cJSON *result = got ? cJSON_Parse(body) : nullptr;
    if (body)
        wipeFree(body);
    const char *error = planString(result, "error", 128);
    if (code != 200 || !result) {
        bool terminal = error && (!strcmp(error, "invalid_grant") || !strcmp(error, "invalid_refresh_token") ||
                                  !strcmp(error, "token_expired") || !strcmp(error, "refresh_token_expired") ||
                                  !strcmp(error, "refresh_token_invalidated") || !strcmp(error, "refresh_token_reused"));
        if (terminal)
            invalidSession.store(true);
        fail(terminal ? "Sessione scaduta o revocata. Ricollega l'account dal Mac."
                      : "Rinnovo non riuscito. Riprova con Internet.");
        cJSON_Delete(result);
        return false;
    }
    const char *access = planString(result, "access_token", 8192), *replacement = planString(result, "refresh_token", 4096);
    const char *scope = planString(result, "scope", 512), *kind = planString(result, "token_type", 32);
    uint32_t seconds = integer(result, "expires_in");
    bool ok = access && replacement && seconds > 0 && seconds <= 86400 && kind && !strcmp(kind, "Bearer") &&
              (!scope || planHasScope(scope, "chatgpt.tokens.use.direct"));
    if (ok) {
        replaceString(credentials, "access_token", access);
        replaceString(credentials, "refresh_token", replacement);
        if (scope)
            replaceString(credentials, "scope", scope);
        replaceNumber(credentials, "expires_at", now + seconds);
        replaceNumber(credentials, "earliest_refresh_at", integer(result, "earliest_refresh_at"));
        ok = planCredentialValid(credentials, host) && persistFromWorker(credentials);
    }
    cJSON_Delete(result);
    if (!ok)
        fail("Rinnovo non salvato. Ricollega l'account dal Mac.");
    return ok;
}
class EventStream : public Stream
{
    ChatGPTPlanEvents &events;

  public:
    EventStream(ChatGPTPlanEvents &parser) : events(parser) {}
    size_t write(uint8_t byte) override { return write(&byte, 1); }
    size_t write(const uint8_t *bytes, size_t n) override
    {
        size_t result = events.write(bytes, n);
        taskYIELD();
        return result;
    }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}
};
bool infer(cJSON *credentials)
{
    const char *access = planString(credentials, "access_token", 8192);
    WiFiClientSecure tls;
    tls.setCACert(kOpenAIRootCA);
    tls.setHandshakeTimeout(15);
    HTTPClient http;
    http.setConnectTimeout(8000);
    http.setTimeout(45000);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    http.setUserAgent("TDeck-Field/1.0 ChatGPT-plan");
    if (!access || !http.begin(tls, "https://api.openai.com/v1/responses"))
        return false;
    http.addHeader("Authorization", String("Bearer ") + access);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Accept", "text/event-stream");
    say("ChatGPT sta rispondendo...");
    int code = http.POST((uint8_t *)pendingAskJSON, strlen(pendingAskJSON));
    wipeFree(pendingAskJSON);
    pendingAskJSON = nullptr;
    if (code != 200) {
        PlanError detail = errorBody(http);
        http.end();
        reportError(detail, code);
        return false;
    }
    char *line = (char *)heap_caps_calloc(32769, 1, MALLOC_CAP_SPIRAM);
    if (!line) {
        http.end();
        fail("Memoria insufficiente.");
        return false;
    }
    ChatGPTPlanEvents events(line, 32769, answer, kReplyMax);
    EventStream sink(events);
    int got = http.writeToStream(&sink);
    http.end();
    bool ok = got > 0 && events.finish();
    wipeFree(line);
    if (!ok) {
        if (events.failed)
            reportError(events.errorCode, 0);
        else
            fail("Risposta interrotta, vuota o troppo lunga. Riprova.");
    }
    return ok;
}
bool revoke(cJSON *credentials)
{
    const char *client = planString(credentials, "client_id", 128),
               *refreshToken = planString(credentials, "refresh_token", 4096);
    if (!client || !refreshToken)
        return false;
    String form = "token_type_hint=refresh_token&client_id=" + encodeForm(client) + "&token=" + encodeForm(refreshToken);
    WiFiClientSecure tls;
    tls.setCACert(kHearHamRootCA);
    tls.setHandshakeTimeout(15);
    HTTPClient http;
    http.setConnectTimeout(8000);
    http.setTimeout(15000);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    if (!http.begin(tls, "https://auth.openai.com/api/accounts/oauth/revoke"))
        return false;
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    int code = http.POST((uint8_t *)form.c_str(), form.length());
    mbedtls_platform_zeroize((void *)form.c_str(), form.length());
    http.end();
    return code == 200;
}
std::atomic<int> logoutResult{0};
void task(void *)
{
    tdeck_tls_reserve_release();
    cJSON *credentials = session ? cJSON_Parse(session) : nullptr;
    bool quitting = logoutRequested.load();
    if (quitting)
        logoutResult.store(credentials && tdeck_wifi_connected() && revoke(credentials) ? 1 : -1);
    else {
        bool ok = credentials && refresh(credentials) && infer(credentials);
        if (ok) {
            snprintf(previousPrompt, 768, "%s", currentPrompt);
            say("Risposta ricevuta.");
            stateValue.store(2);
        } else if (stateValue.load() == 1)
            fail("Connessione ChatGPT non completata. Riprova.");
    }
    cJSON_Delete(credentials);
    wipeFree(pendingAskJSON);
    pendingAskJSON = nullptr;
    tdeck_tls_reserve_take();
    tdeck_network_job_release(1);
    workerActive.store(false, std::memory_order_release);
    vTaskDelete(nullptr);
}
void initialize()
{
    Preferences prefs;
    if (!prefs.begin("tui-siwc", false))
        return;
    prefs.getString("host", host, sizeof(host));
    if (!host[0]) {
        uint8_t bytes[16];
        esp_fill_random(bytes, sizeof(bytes));
        bytes[6] = (bytes[6] & 15) | 64;
        bytes[8] = (bytes[8] & 63) | 128;
        snprintf(host, sizeof(host), "urn:uuid:%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", bytes[0],
                 bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7], bytes[8], bytes[9], bytes[10], bytes[11],
                 bytes[12], bytes[13], bytes[14], bytes[15]);
        if (!prefs.putString("host", host)) {
            host[0] = 0;
            prefs.end();
            return;
        }
    }
    size_t n = prefs.getBytesLength("session");
    if (n && n <= kCredentialMax) {
        session = (char *)heap_caps_calloc(n + 1, 1, MALLOC_CAP_SPIRAM);
        if (session && prefs.getBytes("session", session, n) != n) {
            wipeFree(session);
            session = nullptr;
        }
        cJSON *root = session ? cJSON_Parse(session) : nullptr;
        if (planCredentialValid(root, host)) {
            updateLabels(root);
            ready.store(true);
            say("Pronto. Usa il tuo piano ChatGPT.");
        } else {
            wipeFree(session);
            session = nullptr;
        }
        cJSON_Delete(root);
    }
    prefs.end();
    loaded.store(true, std::memory_order_release);
}
} // namespace
int state()
{
    return stateValue.load(std::memory_order_acquire);
}
bool haveConfig()
{
    return ready.load(std::memory_order_acquire);
}
int configurationResult()
{
    return configResult.load(std::memory_order_acquire);
}
const char *reply()
{
    return answer ? answer : "";
}
const char *statusText()
{
    static char copy[160];
    portENTER_CRITICAL(&messageMux);
    memcpy(copy, message, sizeof(copy));
    portEXIT_CRITICAL(&messageMux);
    return copy;
}
const char *modelName()
{
    return model;
}
const char *accountName()
{
    return account;
}
const char *hostId()
{
    return host;
}
bool probe()
{
    return false;
}
void clear()
{
    if (workerActive.load())
        return;
    if (answer)
        answer[0] = 0;
    if (previousPrompt)
        previousPrompt[0] = 0;
    if (currentPrompt)
        currentPrompt[0] = 0;
    stateValue.store(0);
}
bool ask(const char *text)
{
    if (!text || !*text || strlen(text) >= sizeof(prompt) || workerActive.load() || importing || logoutRequested.load() ||
        persistResult.load() == 0 || pendingSession.load() || !haveConfig())
        return false;
    if (!tdeck_wifi_connected()) {
        fail("Wi-Fi non collegato. Collega Internet e riprova.");
        return false;
    }
    if (!tdeck_network_job_acquire(1)) {
        fail("Rete occupata. Riprova tra poco.");
        return false;
    }
    if (!answer)
        answer = (char *)heap_caps_calloc(kReplyMax, 1, MALLOC_CAP_SPIRAM);
    if (!previousPrompt)
        previousPrompt = (char *)heap_caps_calloc(768, 1, MALLOC_CAP_SPIRAM);
    if (!currentPrompt)
        currentPrompt = (char *)heap_caps_calloc(768, 1, MALLOC_CAP_SPIRAM);
    if (!answer || !previousPrompt || !currentPrompt) {
        tdeck_network_job_release(1);
        fail("Memoria insufficiente.");
        return false;
    }
    cJSON *body = cJSON_CreateObject(), *input = body ? cJSON_AddArrayToObject(body, "input") : nullptr;
    if (!input) {
        cJSON_Delete(body);
        tdeck_network_job_release(1);
        return false;
    }
    auto turn = [input](const char *role, const char *text) {
        cJSON *item = cJSON_CreateObject();
        if (!item)
            return false;
        if (!cJSON_AddStringToObject(item, "role", role) || !cJSON_AddStringToObject(item, "content", text) ||
            !cJSON_AddItemToArray(input, item)) {
            cJSON_Delete(item);
            return false;
        }
        return true;
    };
    bool ok = true;
    if (previousPrompt[0] && answer[0])
        ok = turn("user", previousPrompt) && turn("assistant", answer);
    ok = ok && turn("user", text) && cJSON_AddStringToObject(body, "model", model) &&
         cJSON_AddStringToObject(
             body, "instructions",
             "Rispondi nella lingua dell'utente. Questo dispositivo ha uno schermo 320x240. Usa testo semplice, niente emoji o "
             "markdown. Preferisci tre frasi brevi, salvo richiesta esplicita di dettagli.") &&
         cJSON_AddBoolToObject(body, "store", false) && cJSON_AddBoolToObject(body, "stream", true);
    char *json = ok ? cJSON_PrintUnformatted(body) : nullptr;
    cJSON_Delete(body);
    pendingAskJSON = json && strlen(json) <= 8192 ? copyPSRAM(json) : nullptr;
    cJSON_free(json);
    if (!pendingAskJSON) {
        tdeck_network_job_release(1);
        fail("Richiesta troppo lunga o memoria insufficiente.");
        return false;
    }
    snprintf(currentPrompt, 768, "%s", text);
    stateValue.store(1);
    workerActive.store(true);
    say("Connessione a ChatGPT...");
    pendingAsk.store(true, std::memory_order_release);
    return true;
}
void disconnect()
{
    if (workerActive.load() || importing || persistResult.load() == 0 || pendingSession.load())
        return;
    logoutRequested.store(true);
    stateValue.store(1);
    say("Disconnessione da ChatGPT...");
}
void importCancel()
{
    if (incoming) {
        mbedtls_platform_zeroize(incoming, kCredentialMax + 1);
        free(incoming);
        incoming = nullptr;
    }
    incomingSize = 0;
    importing = false;
}
bool importBegin()
{
    if (!loaded.load() || workerActive.load() || logoutRequested.load() || persistResult.load() == 0 || pendingSession.load())
        return false;
    importCancel();
    incoming = (char *)heap_caps_calloc(kCredentialMax + 1, 1, MALLOC_CAP_SPIRAM);
    importing = incoming != nullptr;
    importResult.store(importing ? 1 : -1);
    return importing;
}
bool importChunk(const char *encoded)
{
    if (!importing || !encoded || strlen(encoded) > 40)
        return false;
    size_t n = 0;
    if (mbedtls_base64_decode((uint8_t *)incoming + incomingSize, kCredentialMax - incomingSize, &n, (const uint8_t *)encoded,
                              strlen(encoded)) ||
        n > 30) {
        importCancel();
        importResult.store(-1);
        return false;
    }
    incomingSize += n;
    incoming[incomingSize] = 0;
    return true;
}
bool importEnd(const char *argument)
{
    unsigned expected;
    char digestText[48] = {}, tail = 0;
    if (!importing || sscanf(argument, "%u %47s %c", &expected, digestText, &tail) != 2 || expected != incomingSize ||
        strlen(digestText) != 43)
        return false;
    for (size_t i = 0; i < 43; i++) {
        if (digestText[i] == '-')
            digestText[i] = '+';
        else if (digestText[i] == '_')
            digestText[i] = '/';
    }
    digestText[43] = '=';
    digestText[44] = 0;
    uint8_t expectedDigest[32], actual[32];
    size_t n = 0;
    bool ok = mbedtls_base64_decode(expectedDigest, sizeof(expectedDigest), &n, (uint8_t *)digestText, 44) == 0 && n == 32 &&
              mbedtls_sha256((uint8_t *)incoming, incomingSize, actual, 0) == 0 && !memcmp(actual, expectedDigest, 32);
    cJSON *root = ok ? cJSON_Parse(incoming) : nullptr;
    ok = ok && planCredentialValid(root, host);
    cJSON_Delete(root);
    if (!ok) {
        importCancel();
        importResult.store(-1);
        return false;
    }
    char *committed = incoming;
    incoming = nullptr;
    incomingSize = 0;
    importing = false;
    persistResult.store(0);
    configResult.store(1);
    importResult.store(2);
    pendingSession.store(committed, std::memory_order_release);
    return true;
}
int importState()
{
    return importResult.load(std::memory_order_acquire);
}
void service()
{
    if (!loaded.load())
        initialize();
    char *incomingSession = pendingSession.exchange(nullptr, std::memory_order_acquire);
    if (incomingSession) {
        size_t n = strlen(incomingSession);
        Preferences prefs;
        bool ok = prefs.begin("tui-siwc", false);
        if (ok) {
            ok = prefs.putBytes("session", incomingSession, n) == n;
            prefs.end();
        }
        if (ok) {
            wipeFree(session);
            session = incomingSession;
            cJSON *root = cJSON_Parse(session);
            updateLabels(root);
            cJSON_Delete(root);
            ready.store(true, std::memory_order_release);
            configResult.store(2);
            if (importResult.load() == 2) {
                importResult.store(3);
                stateValue.store(0);
                say("Account collegato. Usa il tuo piano ChatGPT.");
            }
        } else {
            wipeFree(incomingSession);
            configResult.store(-1);
            if (importResult.load() == 2)
                importResult.store(-1);
            say("Sessione non salvata: spazio NVS insufficiente o errore flash.");
        }
        persistResult.store(ok ? 1 : -1, std::memory_order_release);
    }
    if (invalidSession.load() && !workerActive.load()) {
        Preferences prefs;
        if (prefs.begin("tui-siwc", false)) {
            prefs.remove("session");
            prefs.end();
        }
        wipeFree(session);
        session = nullptr;
        ready.store(false);
        account[0] = model[0] = 0;
        invalidSession.store(false);
    }
    if (pendingAsk.exchange(false)) {
        if (xTaskCreate(task, "chatgpt-plan", 10240, nullptr, 1, nullptr) != pdPASS) {
            wipeFree(pendingAskJSON);
            pendingAskJSON = nullptr;
            workerActive.store(false);
            tdeck_network_job_release(1);
            fail("Memoria insufficiente.");
        }
    }
    if (logoutRequested.load() && !workerActive.load()) {
        int outcome = logoutResult.exchange(0);
        if (outcome) {
            Preferences prefs;
            if (prefs.begin("tui-siwc", false)) {
                prefs.remove("session");
                prefs.end();
            }
            wipeFree(session);
            session = nullptr;
            ready.store(false);
            account[0] = model[0] = 0;
            clear();
            logoutRequested.store(false);
            say(outcome == 1 ? "Account disconnesso."
                             : "Disconnesso localmente. Revoca non confermata: controlla le app nelle impostazioni ChatGPT.");
        } else if (tdeck_network_job_acquire(1)) {
            workerActive.store(true);
            if (xTaskCreate(task, "chatgpt-logout", 8192, nullptr, 1, nullptr) != pdPASS) {
                workerActive.store(false);
                tdeck_network_job_release(1);
                logoutResult.store(-1);
            }
        }
    }
}
} // namespace tdeckchatgpt
