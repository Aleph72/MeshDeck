#pragma once
#include <cJSON.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

inline uint32_t planEpoch(const cJSON *root, const char *name)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(root, name);
    if (cJSON_IsNumber(v))
        return std::isfinite(v->valuedouble) && v->valuedouble >= 0 && v->valuedouble <= 4294967295.0 &&
                       floor(v->valuedouble) == v->valuedouble
                   ? (uint32_t)v->valuedouble
                   : 0;
    if (!cJSON_IsString(v) || !v->valuestring[0])
        return 0;
    char *end = nullptr;
    unsigned long long numeric = strtoull(v->valuestring, &end, 10);
    if (end != v->valuestring && !*end)
        return numeric <= 4294967295ULL ? (uint32_t)numeric : 0;
    int year, month, day, hour, minute, second, used = 0;
    if (sscanf(v->valuestring, "%4d-%2d-%2dT%2d:%2d:%2d%n", &year, &month, &day, &hour, &minute, &second, &used) != 6 ||
        year < 1970 || year > 2106 || month < 1 || month > 12 || hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
        second < 0 || second > 59)
        return 0;
    const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    if (day < 1 || day > days[month - 1] + (month == 2 && leap ? 1 : 0))
        return 0;
    const char *zone = v->valuestring + used;
    if (*zone == '.') {
        zone++;
        while (*zone >= '0' && *zone <= '9')
            zone++;
    }
    int offset = 0;
    if (!strcmp(zone, "Z")) {
    } else if (*zone == '+' || *zone == '-') {
        int h, m, n = 0;
        if (sscanf(zone + 1, "%2d:%2d%n", &h, &m, &n) != 2 || zone[n + 1] || h > 23 || m > 59 || h < 0 || m < 0)
            return 0;
        offset = (h * 60 + m) * 60 * (*zone == '+' ? 1 : -1);
    } else
        return 0;
    year -= month <= 2;
    int era = year / 400;
    unsigned y = year - era * 400;
    unsigned ordinal = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    int64_t totalDays = (int64_t)era * 146097 + y * 365 + y / 4 - y / 100 + ordinal - 719468;
    int64_t seconds = totalDays * 86400 + hour * 3600 + minute * 60 + second - offset;
    return seconds >= 0 && seconds <= 4294967295LL ? (uint32_t)seconds : 0;
}

inline bool planHasScope(const char *scopes, const char *wanted)
{
    if (!scopes || !wanted)
        return false;
    size_t n = strlen(wanted);
    while (*scopes) {
        while (*scopes == ' ')
            scopes++;
        const char *end = strchr(scopes, ' ');
        size_t length = end ? (size_t)(end - scopes) : strlen(scopes);
        if (length == n && !strncmp(scopes, wanted, n))
            return true;
        scopes += length;
    }
    return false;
}

inline const char *planString(const cJSON *root, const char *name, size_t maximum)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(root, name);
    return cJSON_IsString(v) && v->valuestring && v->valuestring[0] && strlen(v->valuestring) <= maximum ? v->valuestring
                                                                                                         : nullptr;
}

inline bool planCredentialValid(const cJSON *root, const char *host)
{
    const char *issuer = planString(root, "issuer", 128), *client = planString(root, "client_id", 128);
    const char *storedHost = planString(root, "ext_agent_host_id", 64), *scopes = planString(root, "scope", 512);
    const char *model = planString(root, "model", 95), *access = planString(root, "access_token", 8192);
    const char *refresh = planString(root, "refresh_token", 4096), *identity = planString(root, "id_token", 8192);
    const cJSON *expires = cJSON_GetObjectItemCaseSensitive(root, "expires_at");
    if (!issuer || strcmp(issuer, "https://auth.openai.com") || !client || strncmp(client, "oaiapp_", 7) || !storedHost ||
        strcmp(storedHost, host) || !planString(root, "subject", 256) || !access || !refresh || !identity || !model ||
        !planHasScope(scopes, "chatgpt.tokens.use.direct") || !planHasScope(scopes, "resource.invoke") ||
        !cJSON_IsNumber(expires) || !std::isfinite(expires->valuedouble) || expires->valuedouble < 1700000000.0 ||
        expires->valuedouble > 4294967295.0 || floor(expires->valuedouble) != expires->valuedouble)
        return false;
    for (const char *p = model; *p; p++)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '-' || *p == '_' ||
              *p == '.'))
            return false;
    for (const char *s : {access, refresh, identity, client})
        for (const char *p = s; *p; p++)
            if ((unsigned char)*p <= 32 || (unsigned char)*p >= 127)
                return false;
    return true;
}

enum class PlanError { OTHER, LIMIT, UNAVAILABLE, INELIGIBLE, AUTH, UNSUPPORTED };
inline PlanError planErrorCode(const char *code)
{
    if (!code)
        return PlanError::OTHER;
    if (!strcmp(code, "subscription_sharing_usage_limit_exceeded") || !strcmp(code, "rate_limit_exceeded"))
        return PlanError::LIMIT;
    if (!strcmp(code, "subscription_sharing_usage_unavailable") || !strcmp(code, "subscription_sharing_user_unavailable"))
        return PlanError::UNAVAILABLE;
    if (!strcmp(code, "subscription_sharing_user_not_eligible"))
        return PlanError::INELIGIBLE;
    if (!strcmp(code, "subscription_sharing_invalid_user") || !strcmp(code, "chatpass_v2_scope_not_authorized") ||
        !strcmp(code, "chatpass_v2_invalid_authorization_context"))
        return PlanError::AUTH;
    if (!strcmp(code, "subscription_sharing_unsupported_capability") || !strcmp(code, "subscription_sharing_route_not_supported"))
        return PlanError::UNSUPPORTED;
    return PlanError::OTHER;
}

class ChatGPTPlanEvents
{
    char *line, *output;
    size_t lineCap, outputCap, lineUsed = 0, outputUsed = 0, total = 0;
    void event()
    {
        if (lineUsed < 5 || strncmp(line, "data:", 5))
            return;
        char *data = line + 5;
        while (*data == ' ')
            data++;
        if (!strcmp(data, "[DONE]"))
            return;
        cJSON *root = cJSON_Parse(data);
        if (!root) {
            valid = false;
            return;
        }
        const char *type = planString(root, "type", 128);
        if (type && (!strcmp(type, "response.output_text.delta") || !strcmp(type, "response.refusal.delta"))) {
            const cJSON *delta = cJSON_GetObjectItemCaseSensitive(root, "delta");
            if (cJSON_IsString(delta)) {
                size_t n = strlen(delta->valuestring), room = outputCap - 1 - outputUsed;
                if (n > room) {
                    n = room;
                    while (n && ((unsigned char)delta->valuestring[n] & 0xc0) == 0x80)
                        n--;
                    truncated = true;
                }
                memcpy(output + outputUsed, delta->valuestring, n);
                outputUsed += n;
                output[outputUsed] = 0;
            }
        } else if (type && !strcmp(type, "response.completed")) {
            const cJSON *response = cJSON_GetObjectItemCaseSensitive(root, "response");
            const char *state = planString(response, "status", 64);
            completed = !state || !strcmp(state, "completed");
            if (!completed)
                failed = true;
        } else if (type && (!strcmp(type, "response.failed") || !strcmp(type, "response.incomplete") || !strcmp(type, "error"))) {
            failed = true;
            const cJSON *response = cJSON_GetObjectItemCaseSensitive(root, "response");
            const cJSON *error = cJSON_GetObjectItemCaseSensitive(response ? response : root, "error");
            errorCode = planErrorCode(planString(error, "code", 128));
        }
        cJSON_Delete(root);
    }

  public:
    bool valid = true, completed = false, failed = false, truncated = false;
    PlanError errorCode = PlanError::OTHER;
    ChatGPTPlanEvents(char *buffer, size_t cap, char *answer, size_t answerCap)
        : line(buffer), output(answer), lineCap(cap), outputCap(answerCap)
    {
        valid = buffer && answer && cap > 8 && answerCap > 1;
        if (valid)
            output[0] = 0;
    }
    size_t write(const unsigned char *bytes, size_t n)
    {
        if (!valid || n > 2 * 1024 * 1024 - total) {
            valid = false;
            return 0;
        }
        total += n;
        for (size_t i = 0; i < n; i++) {
            char c = (char)bytes[i];
            if (c == '\r')
                continue;
            if (c == '\n') {
                line[lineUsed] = 0;
                event();
                lineUsed = 0;
                if (!valid)
                    return 0;
            } else {
                if (lineUsed >= lineCap - 1) {
                    valid = false;
                    return 0;
                }
                line[lineUsed++] = c;
            }
        }
        return n;
    }
    bool finish() const { return valid && completed && !failed && lineUsed == 0 && outputUsed != 0; }
};
