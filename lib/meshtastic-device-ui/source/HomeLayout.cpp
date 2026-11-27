#include "util/HomeLayout.h"
#include <cstdio>
#include <cstring>

extern "C" int tdeck_home_load(void *out, size_t size);
extern "C" bool tdeck_home_save(const void *data, size_t size);

namespace meshhome
{
namespace
{
constexpr uint32_t kMagic = 0x4d484f31;
Layout current{};
bool loaded = false, persisted = false;

uint32_t checksum(const Layout &value)
{
    uint32_t result = 2166136261u;
    const auto *bytes = reinterpret_cast<const uint8_t *>(&value);
    for (size_t i = 0; i < offsetof(Layout, checksum); ++i)
        result = (result ^ bytes[i]) * 16777619u;
    return result;
}

bool validKey(const char (&key)[kKeySize])
{
    if (!key[0] || !memchr(key, 0, kKeySize))
        return false;
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(key); *p; ++p)
        if (*p < 32 || *p == 127)
            return false;
    return true;
}

void load()
{
    if (loaded)
        return;
    loaded = true;
    Layout candidate{};
    if (tdeck_home_load(&candidate, sizeof(candidate)) == static_cast<int>(sizeof(candidate)) && validLayout(candidate)) {
        current = candidate;
        persisted = true;
    }
}

bool commit(Layout &candidate)
{
    candidate.magic = kMagic;
    candidate.checksum = checksum(candidate);
    if (!validLayout(candidate) || !tdeck_home_save(&candidate, sizeof(candidate)))
        return false;
    current = candidate;
    persisted = true;
    return true;
}
} // namespace

bool validChat(Chat chat)
{
    return (chat.kind == Kind::Channel && chat.id < 8) || (chat.kind == Kind::Direct && chat.id >= 8 && chat.id != UINT32_MAX);
}

bool sameChat(Chat a, Chat b)
{
    return a.id == b.id && a.kind == b.kind;
}

void chatKey(Chat chat, char (&key)[kKeySize])
{
    snprintf(key, sizeof(key), "chat:%c%08x", chat.kind == Kind::Channel ? 'c' : 'd', static_cast<unsigned>(chat.id));
}

bool validLayout(const Layout &value)
{
    if (value.magic != kMagic || value.pinCount > kMaxPins || value.orderCount > kMaxOrder || value.checksum != checksum(value))
        return false;
    for (unsigned i = 0; i < value.pinCount; ++i) {
        if (!validChat(value.pins[i]))
            return false;
        for (unsigned j = 0; j < i; ++j)
            if (sameChat(value.pins[i], value.pins[j]))
                return false;
    }
    for (unsigned i = 0; i < value.orderCount; ++i) {
        if (!validKey(value.order[i]))
            return false;
        for (unsigned j = 0; j < i; ++j)
            if (!strcmp(value.order[i], value.order[j]))
                return false;
    }
    return true;
}

const Layout &layout()
{
    load();
    return current;
}

bool saved()
{
    load();
    return persisted;
}

bool pinned(Chat chat)
{
    load();
    for (unsigned i = 0; i < current.pinCount; ++i)
        if (sameChat(chat, current.pins[i]))
            return true;
    return false;
}

bool setPinned(Chat chat, bool on)
{
    load();
    if (!validChat(chat))
        return false;
    if (pinned(chat) == on)
        return true;
    Layout candidate = current;
    if (on) {
        if (candidate.pinCount >= kMaxPins)
            return false;
        candidate.pins[candidate.pinCount++] = {chat.id, chat.kind, {}};
    } else {
        unsigned out = 0;
        for (unsigned i = 0; i < candidate.pinCount; ++i)
            if (!sameChat(chat, candidate.pins[i]))
                candidate.pins[out++] = candidate.pins[i];
        candidate.pinCount = out;
        char key[kKeySize];
        chatKey(chat, key);
        out = 0;
        for (unsigned i = 0; i < candidate.orderCount; ++i)
            if (strcmp(key, candidate.order[i]))
                memmove(candidate.order[out++], candidate.order[i], kKeySize);
        candidate.orderCount = out;
    }
    return commit(candidate);
}

bool setOrder(const char (*keys)[kKeySize], unsigned count)
{
    load();
    if (count > kMaxOrder || (count && !keys))
        return false;
    if (count == current.orderCount && (!count || !memcmp(keys, current.order, count * kKeySize)))
        return true;
    Layout candidate = current;
    memset(candidate.order, 0, sizeof(candidate.order));
    candidate.orderCount = count;
    if (count)
        memcpy(candidate.order, keys, count * kKeySize);
    return commit(candidate);
}

void seedLegacyOrder(const char (*keys)[kKeySize], unsigned count)
{
    load();
    if (persisted || current.orderCount || count > kMaxOrder || (count && !keys))
        return;
    Layout candidate = current;
    candidate.magic = kMagic;
    candidate.orderCount = count;
    if (count)
        memcpy(candidate.order, keys, count * kKeySize);
    candidate.checksum = checksum(candidate);
    if (validLayout(candidate))
        current = candidate;
}
} // namespace meshhome
