#include "graphics/view/TFT/LauncherIcons.h"
#include "graphics/view/TFT/TuiLabel.h"
#include "graphics/view/TFT/TuiStatusBar.h"
#include "graphics/view/TFT/UnreadCounts.h"
#include "util/HomeLayout.h"
#include "util/FieldLocale.h"
static const char *fixtureLocale = "it";
extern "C" const char *lv_i18n_get_current_locale(void) { return fixtureLocale; }
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <lvgl.h>
#include <map>
#include <string>
#include <vector>
#define HAS_SDCARD 0
#define HAS_SD_MMC 0
#define ARCH_PORTDUINO 0
static std::vector<uint8_t> savedBytes;
static unsigned writes = 0;
static bool failSave = false;
extern "C" int tdeck_home_load(void *out, size_t n)
{
    if (savedBytes.size() != n)
        return 0;
    memcpy(out, savedBytes.data(), n);
    return n;
}
extern "C" bool tdeck_home_save(const void *p, size_t n)
{
    if (failSave)
        return false;
    auto *b = (const uint8_t *)p;
    savedBytes.assign(b, b + n);
    ++writes;
    return true;
}
volatile bool tdeck_back_request = false;
static std::string groupName = "Gruppo locale";
constexpr uint32_t privateId = 0x8abc1234;
extern "C" int tdeck_channel_count()
{
    return 2;
}
extern "C" int tdeck_channel_role(int i)
{
    return i < 2 ? i + 1 : 0;
}
extern "C" const char *tdeck_channel_name(int i)
{
    return i == 0 ? groupName.c_str() : "Secondo canale";
}
extern "C" const char *tdeck_node_name(uint32_t id)
{
    return id == privateId ? "Contatto prova" : "Altro contatto";
}
extern "C" int tui_dm_conversations(uint32_t *out, int max)
{
    assert(max >= 2);
    out[0] = privateId;
    out[1] = 0x12345678;
    return 2;
}
extern "C" void notif_clear_one(uint32_t, uint8_t, bool) {}
void tui_statusbar_reserve(lv_obj_t *s)
{
    lv_obj_set_style_pad_top(s, 20, 0);
}
static lv_obj_t *chatScreen;
static uint32_t openedId = 0;
static int openedKind = 0;
static std::string openedApp;
static int img_home_button_image, img_map_button_image, img_nodes_button_image, img_messages_button_image,
    img_settings_button_image;
struct {
    lv_obj_t *home_button = nullptr, *home_panel = nullptr, *top_panel = nullptr, *main_screen = nullptr;
} objects;
constexpr int meshtastic_Config_LoRaConfig_RegionCode_UNSET = 0;
constexpr int c_max_channels = 8;
extern "C" void calculator_open()
{
    openedApp = "Calculator";
}
extern "C" void chatgpt_open()
{
    openedApp = "ChatGPT";
}
extern "C" void repeaters_open()
{
    openedApp = "Ripetitori";
}
extern "C" void weather_open()
{
    openedApp = "Meteo";
}
extern "C" void updates_open()
{
    openedApp = "Updates";
}
class TFTView_320x240;
static TFTView_320x240 *view;
#define THIS TFTView_320x240::instance()
class TFTView_320x240
{
  public:
    static TFTView_320x240 *instance() { return view; }
    static void openMapsAction() { openedApp = "Maps"; }
    static void openSettingsAction() { openedApp = "Settings"; }
    static void tuiHomeChanged();
    struct LaunchDesc {
        char name[24];
        uint32_t color;
        int builtinIdx, userIdx;
        uint32_t chatId;
        bool chatChannel;
    };
    LaunchDesc launchList[48]{};
    int launchCount = 0;
    char appOrder[48][24]{};
    int appOrderCount = 0;
    char userAppDirs[26][24]{};
    bool suppressTileClick = false, launcherRebuildQueued = false, messagesRestored = true;
    int launcherNextPage = -1;
    lv_obj_t *launcher_screen = nullptr, *launcher_pager = nullptr, *launcher_dots = nullptr;
    lv_obj_t *arrange_overlay = nullptr, *arrange_list = nullptr, *arrange_status = nullptr;
    lv_obj_t *launcher_chat_badges[48]{};
    lv_obj_t *channel[8]{};
    std::map<uint32_t, lv_obj_t *> nodes;
    unsigned unreadMessages = 0;
    struct {
        struct {
            struct {
                int region = 1;
            } lora;
        } config;
    } db;
    void runUserApp(int) {}
    void ui_set_active(lv_obj_t *, lv_obj_t *, lv_obj_t *) {}
    void requestSetup() {}
    void updateUnreadMessages() { refreshHomeBadges(); }
    void openChannelChat(uint8_t id)
    {
        openedKind = 1;
        openedId = id;
        lv_screen_load(chatScreen);
    }
    void openChatWithNode(uint32_t id)
    {
        openedKind = 2;
        openedId = id;
        lv_screen_load(chatScreen);
    }
    void applyAppOrder();
    bool saveAppOrder();
    void buildAppGrid();
    void rebuildAppGrid();
    void closeArrange();
    void refreshArrangeRows();
    void moveAppInArrange(int, int);
    void openArrange();
    const char *launcherTitle(const LaunchDesc &);
    void buildHomeControls();
    void refreshHomeBadges();
    void openHomeChat(const LaunchDesc &);
};
#include "launcher-table.inc"
static uint32_t tintAccent(uint32_t base, uint32_t accent, int pct)
{
    uint32_t result = 0;
    for (int s : {0, 8, 16})
        result |= (((base >> s & 255) * (100 - pct) + (accent >> s & 255) * pct) / 100) << s;
    return result;
}
static bool buildTileIconFromFile(lv_obj_t *, const char *)
{
    return false;
}
static void buildTileIcon(lv_obj_t *tile, const char *name, uint32_t color)
{
    auto *canvas = lv_obj_create(tile);
    lv_obj_remove_style_all(canvas);
    lv_obj_set_size(canvas, 46, 40);
    lv_obj_align(canvas, LV_ALIGN_TOP_MID, 0, 4);
    lv_obj_clear_flag(canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(canvas, LV_OBJ_FLAG_SCROLLABLE);
    assert(meshdeck::drawLauncherIcon(canvas, name, color));
}
#include "launcher-methods.inc"
extern "C" void tui_home_changed()
{
    TFTView_320x240::tuiHomeChanged();
}
static uint16_t pixels[320 * 240], buffer[320 * 240];
static void flush(lv_display_t *d, const lv_area_t *a, uint8_t *map)
{
    auto *p = (uint16_t *)map;
    for (int y = a->y1; y <= a->y2; ++y)
        for (int x = a->x1; x <= a->x2; ++x)
            pixels[y * 320 + x] = *p++;
    lv_display_flush_ready(d);
}
static void pump()
{
    for (int i = 0; i < 5; ++i) {
        lv_tick_inc(20);
        lv_timer_handler();
    }
}
static void shot(const char *path, lv_display_t *d)
{
    lv_obj_update_layout(lv_screen_active());
    lv_refr_now(d);
    auto *f = fopen(path, "wb");
    assert(f);
    fprintf(f, "P6\n320 240\n255\n");
    for (auto c : pixels) {
        uint8_t rgb[] = {uint8_t(((c >> 11) & 31) * 255 / 31), uint8_t(((c >> 5) & 63) * 255 / 63), uint8_t((c & 31) * 255 / 31)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}
static lv_obj_t *findText(lv_obj_t *root, const char *text)
{
    if (lv_obj_check_type(root, &lv_label_class) && !strcmp(lv_label_get_text(root), text))
        return root;
    for (unsigned i = 0; i < lv_obj_get_child_count(root); ++i)
        if (auto *found = findText(lv_obj_get_child(root, i), text))
            return found;
    return nullptr;
}
static void choose(const char *name)
{
    auto *label = findText(lv_screen_active(), name);
    assert(label);
    auto *row = lv_obj_get_parent(label);
    lv_obj_send_event(lv_obj_get_child(row, 2), LV_EVENT_CLICKED, nullptr);
    pump();
}
static lv_obj_t *tile(int i)
{
    auto *page = lv_obj_get_child(view->launcher_pager, i / 6);
    return lv_obj_get_child(page, i % 6);
}
static std::vector<std::string> keys()
{
    std::vector<std::string> out;
    for (int i = 0; i < view->launchCount; ++i)
        out.emplace_back(view->launchList[i].name);
    return out;
}
int main()
{
    lv_init();
    auto *display = lv_display_create(320, 240);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, buffer, nullptr, sizeof(buffer), LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, flush);
    lv_display_set_theme(display, lv_theme_default_init(display, lv_palette_main(LV_PALETTE_BLUE),
                                                        lv_palette_main(LV_PALETTE_RED), false, LV_FONT_DEFAULT));
    auto *group = lv_group_create();
    lv_group_set_default(group);
    TFTView_320x240 instance;
    view = &instance;
    view->launcher_screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(view->launcher_screen, lv_color_hex(0x000000), 0);
    chatScreen = lv_obj_create(nullptr);
    view->nodes[privateId] = chatScreen;
    view->channel[0] = chatScreen;
    lv_screen_load(view->launcher_screen);
    view->buildAppGrid();
    view->buildHomeControls();
    pump();
    assert(view->launchCount == 8);
    auto *preferite = findText(view->launcher_screen, "Preferite");
    assert(preferite);
    lv_obj_send_event(lv_obj_get_parent(preferite), LV_EVENT_CLICKED, nullptr);
    pump();
    choose("Gruppo locale");
    choose("Contatto prova");
    assert(meshhome::layout().pinCount == 2 && writes == 2);
    shot("choose-chats.ppm", display);
    assert(home_chats_back());
    pump();
    assert(view->launchCount == 10);
    unread_add(unreadKeyForChannel(0));
    unread_add(unreadKeyForChannel(0));
    unread_add(privateId);
    view->refreshHomeBadges();
    assert(!strcmp(lv_label_get_text(view->launcher_chat_badges[0]), "2"));
    assert(!strcmp(lv_label_get_text(view->launcher_chat_badges[1]), "1"));
    shot("home-pins.ppm", display);
    lv_obj_send_event(tile(0), LV_EVENT_CLICKED, nullptr);
    pump();
    assert(openedKind == 1 && openedId == 0);
    assert(unread_get(privateId) == 1 && unread_get(unreadKeyForChannel(0)) == 0);
    lv_screen_load(view->launcher_screen);
    view->messagesRestored = false;
    lv_obj_send_event(tile(1), LV_EVENT_CLICKED, nullptr);
    pump();
    assert(findText(lv_screen_active(), "Chat in Home"));
    assert(unread_get(privateId) == 1);
    assert(home_chats_back());
    view->messagesRestored = true;
    lv_obj_send_event(tile(1), LV_EVENT_CLICKED, nullptr);
    pump();
    assert(openedKind == 2 && openedId == privateId);
    assert(unread_get(privateId) == 0);
    lv_screen_load(view->launcher_screen);
    auto *ordina = findText(view->launcher_screen, "Ordina");
    lv_obj_send_event(lv_obj_get_parent(ordina), LV_EVENT_CLICKED, nullptr);
    pump();
    shot("arrange.ppm", display);
    auto old = keys();
    auto *first = lv_obj_get_child(view->arrange_list, 0);
    lv_obj_send_event(lv_obj_get_child(first, 5), LV_EVENT_CLICKED, nullptr);
    pump();
    assert(view->launchList[6].chatId == 0 && view->launchList[6].builtinIdx == -1);
    assert(lv_obj_get_scroll_x(view->launcher_pager) == 320);
    assert(view->launchCount == 10);
    auto expected = old;
    auto moved = expected[0];
    expected.erase(expected.begin());
    expected.insert(expected.begin() + 6, moved);
    assert(keys() == expected);
    auto *returnRow = lv_obj_get_child(view->arrange_list, 6);
    lv_obj_send_event(lv_obj_get_child(returnRow, 4), LV_EVENT_CLICKED, nullptr);
    pump();
    assert(keys() == old);
    first = lv_obj_get_child(view->arrange_list, 0);
    lv_obj_send_event(lv_obj_get_child(first, 5), LV_EVENT_CLICKED, nullptr);
    pump();
    assert(keys() == expected);
    old = keys();
    moved = old[5];
    view->moveAppInArrange(5, 6);
    pump();
    assert(view->launchList[9].name == moved);
    expected = old;
    expected.erase(expected.begin() + 5);
    expected.push_back(moved);
    assert(keys() == expected);
    old = keys();
    auto n = writes;
    failSave = true;
    view->moveAppInArrange(0, 1);
    pump();
    assert(keys() == old && writes == n);
    assert(strstr(lv_label_get_text(view->arrange_status), "non riuscito"));
    failSave = false;
    view->moveAppInArrange(0, -6);
    assert(keys() == old);
    view->moveAppInArrange(9, 6);
    assert(keys() == old);
    view->moveAppInArrange(-1, 1);
    assert(keys() == old);
    view->closeArrange();
    pump();
    view->rebuildAppGrid();
    pump();
    assert(keys() == old);
    assert(lv_obj_get_scroll_x(view->launcher_pager) == 320);
    shot("home-page-two.ppm", display);
    groupName = "Gruppo rinominato";
    view->rebuildAppGrid();
    pump();
    assert(keys() == old);
    bool renamed = false;
    for (int i = 0; i < view->launchCount; ++i)
        if (view->launchList[i].builtinIdx < 0 && view->launchList[i].userIdx < 0 && view->launchList[i].chatChannel)
            renamed = !strcmp(view->launcherTitle(view->launchList[i]), "Gruppo rinominato");
    assert(renamed);
    home_chats_open();
    pump();
    n = writes;
    failSave = true;
    choose("Secondo canale");
    assert(writes == n && meshhome::layout().pinCount == 2);
    assert(findText(lv_screen_active(), "Salvataggio non riuscito. Riprova."));
    failSave = false;
    choose("Gruppo rinominato");
    assert(meshhome::layout().pinCount == 1);
    assert(home_chats_back());
    pump();
    assert(view->launchCount == 9);
    for (int i = 0; i < 3; ++i) {
        home_chats_open();
        pump();
        assert(home_chats_back());
        pump();
    }
    lv_mem_monitor_t before, after;
    lv_mem_monitor(&before);
    for (int i = 0; i < 20; ++i) {
        home_chats_open();
        pump();
        assert(home_chats_back());
        pump();
    }
    lv_mem_monitor(&after);
    fixtureLocale = "es";
    view->rebuildAppGrid();
    pump();
    assert(findText(view->launcher_screen, "Mapas"));
    home_chats_open();
    pump();
    assert(findText(lv_screen_active(), "Chats en inicio"));
    assert(home_chats_back());
    fixtureLocale = "en";
    home_chats_open();
    pump();
    assert(findText(lv_screen_active(), "Home chats"));
    assert(home_chats_back());

    printf("PASS: real Home grid/chooser/arrange callbacks, channel and DM targets, per-chat unread, insertion across partial "
           "pages, rollback, rename-stable keys, return navigation. Native LVGL free delta after 20 reopenings: %ld B.\n",
           long(after.free_size) - long(before.free_size));
}
