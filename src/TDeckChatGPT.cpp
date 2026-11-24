#include "configuration.h"
#include "util/OpenAIReply.h"
#include <Preferences.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <ctype.h>
#include <esp_heap_caps.h>

extern "C" bool tdeck_net_openai_post(const char *, const char *);
extern "C" int tdeck_net_poll(void);
extern "C" int tdeck_net_result(char *, int);
extern "C" int tdeck_net_http_code(void);
extern "C" void tdeck_net_reset(void);

namespace tdeckchatgpt
{
enum { IDLE, WORKING, DONE, FAILED };
static std::atomic<int> stateValue{IDLE};
static std::atomic<bool> pending{false}, ready{false}, loaded{false}, configPending{false};
static std::atomic<int> configResult{0};
static char key[512] = {}, model[64] = "gpt-4.1-mini";
static char pendingKey[512] = {}, pendingModel[64] = {};
static char prompt[768] = {}, status[144] = {};
static char *answer = nullptr;
static char *previousPrompt = nullptr, *currentPrompt = nullptr;
static bool ownRequest = false;
static std::atomic<bool> probePending{false};
static bool probing = false;
bool probe()
{
    if (stateValue.load() == WORKING)
        return false;
    stateValue.store(WORKING);
    probePending.store(true);
    return true;
}

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
    return status;
}
const char *modelName()
{
    return loaded.load(std::memory_order_acquire) ? model : "gpt-4.1-mini";
}

static void fail(const char *text)
{
    snprintf(status, sizeof(status), "%s", text);
    stateValue.store(FAILED, std::memory_order_release);
}

bool saveConfig(const char *newKey, const char *newModel)
{
    if (!newKey || !newModel || state() == WORKING || configPending.load() || strlen(newKey) >= sizeof(pendingKey) ||
        !*newModel || strlen(newModel) >= sizeof(pendingModel))
        return false;
    for (const char *p = newKey; *p; p++)
        if (!(isalnum((unsigned char)*p) || *p == '-' || *p == '_'))
            return false;
    for (const char *p = newModel; *p; p++)
        if (!(isalnum((unsigned char)*p) || *p == '-' || *p == '_' || *p == '.'))
            return false;
    snprintf(pendingKey, sizeof(pendingKey), "%s", newKey);
    snprintf(pendingModel, sizeof(pendingModel), "%s", newModel);
    configResult.store(1);
    configPending.store(true, std::memory_order_release);
    return true;
}

bool ask(const char *text)
{
    if (!text || !*text || strlen(text) >= sizeof(prompt) || state() == WORKING || !haveConfig() || configPending.load())
        return false;
    snprintf(prompt, sizeof(prompt), "%s", text);
    snprintf(status, sizeof(status), "Connessione a OpenAI...");
    stateValue.store(WORKING, std::memory_order_release);
    pending.store(true, std::memory_order_release);
    return true;
}

void clear()
{
    if (state() == WORKING)
        return;
    if (answer)
        answer[0] = 0;
    if (previousPrompt)
        previousPrompt[0] = 0;
    if (currentPrompt)
        currentPrompt[0] = 0;
    memset(prompt, 0, sizeof(prompt));
    status[0] = 0;
    stateValue.store(IDLE, std::memory_order_release);
}

void service()
{
    if (!loaded.load()) {
        Preferences prefs;
        if (prefs.begin("tui-openai", true)) {
            prefs.getString("key", key, sizeof(key));
            prefs.getString("model", model, sizeof(model));
            prefs.end();
        }
        if (!model[0])
            snprintf(model, sizeof(model), "gpt-4.1-mini");
        ready.store(key[0] != 0, std::memory_order_release);
        loaded.store(true, std::memory_order_release);
    }
    if (configPending.load(std::memory_order_acquire)) {
        Preferences prefs;
        bool ok = prefs.begin("tui-openai", false);
        if (ok) {
            if (pendingKey[0])
                ok = prefs.putString("key", pendingKey) != 0;
            ok = (prefs.putString("model", pendingModel) != 0) && ok;
            prefs.end();
        }
        if (ok) {
            if (pendingKey[0])
                snprintf(key, sizeof(key), "%s", pendingKey);
            snprintf(model, sizeof(model), "%s", pendingModel);
            ready.store(key[0] != 0, std::memory_order_release);
        }
        memset(pendingKey, 0, sizeof(pendingKey));
        configResult.store(ok ? 2 : -1, std::memory_order_release);
        configPending.store(false, std::memory_order_release);
    }
    if (probePending.exchange(false)) {
        snprintf(status, sizeof(status), "Verifico HTTPS OpenAI...");
        probing = tdeck_net_openai_post(
            "{\"model\":\"gpt-4.1-mini\",\"input\":\"Connectivity probe\",\"max_output_tokens\":16,\"store\":false}",
            "sk-field-connectivity-probe-invalid");
        ownRequest = probing;
        if (!probing)
            fail("Rete occupata. Riprova il test.");
    }
    if (pending.exchange(false, std::memory_order_acquire)) {
        if (!previousPrompt)
            previousPrompt = (char *)heap_caps_calloc(768, 1, MALLOC_CAP_SPIRAM);
        if (!currentPrompt)
            currentPrompt = (char *)heap_caps_calloc(768, 1, MALLOC_CAP_SPIRAM);
        if (!previousPrompt || !currentPrompt) {
            fail("Memoria insufficiente");
            return;
        }
        snprintf(currentPrompt, 768, "%s", prompt);
        cJSON *body = cJSON_CreateObject();
        if (!body) {
            fail("Memoria insufficiente");
            return;
        }
        cJSON_AddStringToObject(body, "model", model);
        cJSON *input = cJSON_AddArrayToObject(body, "input");
        if (!input) {
            cJSON_Delete(body);
            fail("Memoria insufficiente");
            return;
        }
        bool turnsOK = true;
        auto addTurn = [input, &turnsOK](const char *role, const char *text) {
            cJSON *turn = cJSON_CreateObject();
            if (!turn) {
                turnsOK = false;
                return;
            }
            if (!cJSON_AddStringToObject(turn, "role", role) || !cJSON_AddStringToObject(turn, "content", text) ||
                !cJSON_AddItemToArray(input, turn)) {
                cJSON_Delete(turn);
                turnsOK = false;
            }
        };
        if (previousPrompt[0] && answer && answer[0]) {
            addTurn("user", previousPrompt);
            addTurn("assistant", answer);
        }
        addTurn("user", prompt);
        cJSON_AddStringToObject(body, "instructions",
                                "Reply in the user's language. Use plain text, no markdown or emoji. "
                                "This is a 320x240 handheld: prefer three brief sentences unless more detail is requested.");
        cJSON_AddNumberToObject(body, "max_output_tokens", 450);
        cJSON_AddBoolToObject(body, "store", false);
        char *json = turnsOK && cJSON_GetArraySize(body) == 5 ? cJSON_PrintUnformatted(body) : nullptr;
        bool ok = json && tdeck_net_openai_post(json, key);
        cJSON_free(json);
        cJSON_Delete(body);
        memset(prompt, 0, sizeof(prompt));
        if (!ok) {
            fail("Rete occupata o Wi-Fi non configurato. Riprova.");
            return;
        }
        ownRequest = true;
    }
    if (!ownRequest)
        return;
    int net = tdeck_net_poll();
    if (net == 2) {
        if (probing) {
            probing = false;
            ownRequest = false;
            tdeck_net_reset();
            fail("Risposta inattesa al test HTTPS.");
            return;
        }
        char *raw = (char *)heap_caps_malloc(8193, MALLOC_CAP_SPIRAM);
        if (!answer)
            answer = (char *)heap_caps_malloc(2401, MALLOC_CAP_SPIRAM);
        if (!raw || !answer) {
            if (raw)
                free(raw);
            tdeck_net_reset();
            ownRequest = false;
            fail("Memoria insufficiente");
            return;
        }
        int n = tdeck_net_result(raw, 8193);
        bool ok = n > 0 && openaiReplyText(raw, answer, 2401);
        free(raw);
        ownRequest = false;
        if (!ok) {
            fail("Risposta non leggibile");
            return;
        }
        status[0] = 0;
        snprintf(previousPrompt, 768, "%s", currentPrompt);
        stateValue.store(DONE, std::memory_order_release);
    } else if (net == 3) {
        int code = tdeck_net_http_code();
        tdeck_net_reset();
        ownRequest = false;
        if (probing && code == 401) {
            probing = false;
            snprintf(status, sizeof(status), "HTTPS OpenAI verificato (HTTP 401, nessuna chiave reale).");
            stateValue.store(DONE, std::memory_order_release);
            return;
        }
        probing = false;
        if (code == 401)
            fail("Chiave API non valida. Apri Config.");
        else if (code == 429)
            fail("Credito API o limite richieste. Controlla il tuo account OpenAI.");
        else if (code == 400 || code == 404)
            fail("Modello o richiesta non disponibili. Controlla Config.");
        else
            fail("Connessione non riuscita. Verifica Wi-Fi, Internet e ora.");
    }
}
} // namespace tdeckchatgpt
