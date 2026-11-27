#pragma once
#include <cstddef>
#include <cstdint>

namespace meshhome
{
constexpr unsigned kMaxPins = 12, kMaxOrder = 48, kKeySize = 24, kPerPage = 6;
enum class Kind : uint8_t { Channel = 1, Direct = 2 };
struct Chat {
    uint32_t id;
    Kind kind;
    uint8_t reserved[3];
};
struct Layout {
    uint32_t magic;
    uint16_t pinCount, orderCount;
    Chat pins[kMaxPins];
    char order[kMaxOrder][kKeySize];
    uint32_t checksum;
};
bool validChat(Chat chat);
bool sameChat(Chat a, Chat b);
void chatKey(Chat chat, char (&key)[kKeySize]);
bool validLayout(const Layout &layout);
const Layout &layout();
bool saved();
bool pinned(Chat chat);
bool setPinned(Chat chat, bool on);
bool setOrder(const char (*keys)[kKeySize], unsigned count);
void seedLegacyOrder(const char (*keys)[kKeySize], unsigned count);
} // namespace meshhome

extern "C" void home_chats_open(void);
extern "C" bool home_chats_back(void);
extern "C" void home_chats_notice(const char *message);
extern "C" void tui_home_changed(void);
