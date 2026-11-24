#pragma once
#include <cJSON.h>
#include <cstring>

// Read only user-visible text from Responses API output, never error details.
inline bool openaiReplyText(const char *json, char *out, size_t cap)
{
    if (!json || !out || cap < 2)
        return false;
    out[0] = 0;
    cJSON *root = cJSON_Parse(json);
    if (!root)
        return false;
    size_t used = 0;
    const cJSON *item = nullptr;
    cJSON_ArrayForEach(item, cJSON_GetObjectItemCaseSensitive(root, "output"))
    {
        const cJSON *type = cJSON_GetObjectItemCaseSensitive(item, "type");
        if (!cJSON_IsString(type) || strcmp(type->valuestring, "message"))
            continue;
        const cJSON *part = nullptr;
        cJSON_ArrayForEach(part, cJSON_GetObjectItemCaseSensitive(item, "content"))
        {
            const cJSON *kind = cJSON_GetObjectItemCaseSensitive(part, "type");
            if (!cJSON_IsString(kind))
                continue;
            const cJSON *value = cJSON_GetObjectItemCaseSensitive(part, !strcmp(kind->valuestring, "output_text") ? "text"
                                                                        : !strcmp(kind->valuestring, "refusal")   ? "refusal"
                                                                                                                  : "");
            if (!cJSON_IsString(value) || !value->valuestring[0])
                continue;
            if (used && used < cap - 1)
                out[used++] = '\n';
            size_t n = strlen(value->valuestring);
            if (n > cap - 1 - used) {
                n = cap - 1 - used;
                while (n && (static_cast<unsigned char>(value->valuestring[n]) & 0xc0) == 0x80)
                    n--;
            }
            memcpy(out + used, value->valuestring, n);
            used += n;
            out[used] = 0;
        }
    }
    cJSON_Delete(root);
    return used != 0;
}
