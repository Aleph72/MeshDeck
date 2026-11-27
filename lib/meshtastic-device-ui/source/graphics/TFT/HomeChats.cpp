#include "graphics/view/TFT/TuiLabel.h"
#include "graphics/view/TFT/TuiStatusBar.h"
#include "util/FieldLocale.h"
#include "util/HomeLayout.h"
#include <cstdio>
#include <lvgl.h>

extern volatile bool tdeck_back_request;
extern "C" int tdeck_channel_count(void);
extern "C" int tdeck_channel_role(int index);
extern "C" const char *tdeck_channel_name(int index);
extern "C" const char *tdeck_node_name(uint32_t id);
extern "C" int tui_dm_conversations(uint32_t *out, int max);

namespace
{
constexpr unsigned kMaxChoices = meshhome::kMaxPins + 8 + 24;
meshhome::Chat choices[kMaxChoices]{};
unsigned choiceCount = 0;
lv_obj_t *title = nullptr, *doneLabel = nullptr;
lv_obj_t *screen = nullptr, *previous = nullptr, *list = nullptr, *status = nullptr;

void updateStatus()
{
    char text[80];
    snprintf(
        text, sizeof(text),
        fieldText("%u/%u on Home. Tap + or Remove.", "%u/%u in Home. Tocca + o Rimuovi.", "%u/%u en inicio. Pulsa + o Quitar."),
        static_cast<unsigned>(meshhome::layout().pinCount), meshhome::kMaxPins);
    lv_label_set_text(status, text);
}

void addChoice(meshhome::Chat chat)
{
    if (!meshhome::validChat(chat) || choiceCount == kMaxChoices)
        return;
    for (unsigned i = 0; i < choiceCount; ++i)
        if (meshhome::sameChat(chat, choices[i]))
            return;
    choices[choiceCount++] = chat;
}

void buildList()
{
    lv_obj_clean(list);
    choiceCount = 0;
    const auto &saved = meshhome::layout();
    for (unsigned i = 0; i < saved.pinCount; ++i)
        addChoice(saved.pins[i]);
    for (int i = 0; i < tdeck_channel_count() && i < 8; ++i)
        if (tdeck_channel_role(i))
            addChoice({static_cast<uint32_t>(i), meshhome::Kind::Channel, {}});
    uint32_t conversations[24]{};
    const int count = tui_dm_conversations(conversations, 24);
    for (int i = 0; i < count && i < 24; ++i)
        addChoice({conversations[i], meshhome::Kind::Direct, {}});

    for (unsigned i = 0; i < choiceCount; ++i) {
        const auto chat = choices[i];
        auto *row = lv_obj_create(list);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, 302, 48);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x1c1c1e), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(row, 8, 0);
        auto *name = lv_label_create(row);
        lv_obj_set_pos(name, 10, 5);
        lv_obj_set_width(name, 204);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        tui_one_line(name);
        lv_label_set_text(name, chat.kind == meshhome::Kind::Channel ? tdeck_channel_name(chat.id) : tdeck_node_name(chat.id));
        lv_obj_set_style_text_color(name, lv_color_hex(0xffffff), 0);
        auto *kind = lv_label_create(row);
        lv_obj_set_pos(kind, 10, 27);
        lv_obj_set_style_text_font(kind, &ui_font_montserrat_12, 0);
        lv_obj_set_style_text_color(kind, lv_color_hex(0x8e8e93), 0);
        lv_label_set_text(kind, chat.kind == meshhome::Kind::Channel ? fieldText("Channel", "Canale", "Canal")
                                                                     : fieldText("Direct chat", "Chat privata", "Chat privada"));

        auto *toggle = lv_button_create(row);
        lv_obj_set_pos(toggle, 222, 6);
        lv_obj_set_size(toggle, 74, 36);
        lv_obj_set_style_pad_all(toggle, 2, 0);
        auto *text = lv_label_create(toggle);
        lv_obj_set_style_text_font(text, &ui_font_montserrat_12, 0);
        lv_label_set_text(text, meshhome::pinned(chat) ? fieldText("Remove", "Rimuovi", "Quitar") : "+");
        lv_obj_center(text);
        lv_obj_add_event_cb(
            toggle,
            [](lv_event_t *event) {
                const unsigned index = reinterpret_cast<uintptr_t>(lv_event_get_user_data(event));
                if (index >= choiceCount)
                    return;
                const auto selected = choices[index];
                const bool on = !meshhome::pinned(selected);
                if (on && meshhome::layout().pinCount >= meshhome::kMaxPins) {
                    lv_label_set_text(status,
                                      fieldText("Home is full. Remove a favorite first.", "Home piena. Rimuovi una preferita.",
                                                "Inicio lleno. Quita un favorito."));
                    return;
                }
                if (!meshhome::setPinned(selected, on)) {
                    lv_label_set_text(status, fieldText("Could not save. Try again.", "Salvataggio non riuscito. Riprova.",
                                                        "No se pudo guardar. Inténtalo de nuevo."));
                    return;
                }
                auto *button = static_cast<lv_obj_t *>(lv_event_get_current_target(event));
                lv_label_set_text(lv_obj_get_child(button, 0), on ? fieldText("Remove", "Rimuovi", "Quitar") : "+");
                updateStatus();
                tui_home_changed();
            },
            LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
    }
    auto *hint = lv_label_create(list);
    lv_obj_set_width(hint, 296);
    lv_obj_set_style_text_font(hint, &ui_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x8e8e93), 0);
    lv_label_set_text(hint,
                      fieldText("Open a direct chat in Mesh first. Favorites are saved even without an SD card.",
                                "Per aggiungere una chat privata, aprila prima in Mesh. Le preferite si salvano anche senza SD.",
                                "Abre primero el chat privado en Mesh. Los favoritos se guardan incluso sin tarjeta SD."));
    updateStatus();
}
} // namespace

extern "C" bool home_chats_back(void)
{
    if (!screen || lv_screen_active() != screen)
        return false;
    if (previous && lv_obj_is_valid(previous)) {
        lv_screen_load(previous);
        return true;
    }
    return false;
}

extern "C" void home_chats_notice(const char *message)
{
    if (status)
        lv_label_set_text(status, message);
}

extern "C" void home_chats_open(void)
{
    if (!screen) {
        screen = lv_obj_create(nullptr);
        lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
        tui_statusbar_reserve(screen);
        title = lv_label_create(screen);
        lv_obj_set_pos(title, 8, 8);
        lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
        auto *done = lv_button_create(screen);
        lv_obj_set_pos(done, 246, 0);
        lv_obj_set_size(done, 68, 30);
        doneLabel = lv_label_create(done);
        lv_obj_set_style_text_font(doneLabel, &ui_font_montserrat_12, 0);
        lv_obj_center(doneLabel);
        lv_obj_add_event_cb(done, [](lv_event_t *) { tdeck_back_request = true; }, LV_EVENT_CLICKED, nullptr);
        status = lv_label_create(screen);
        lv_obj_set_pos(status, 8, 38);
        lv_obj_set_width(status, 304);
        lv_label_set_long_mode(status, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(status, &ui_font_montserrat_12, 0);
        lv_obj_set_style_text_color(status, lv_color_hex(0x8e8e93), 0);
        tui_one_line(status);
        list = lv_obj_create(screen);
        lv_obj_remove_style_all(list);
        lv_obj_set_pos(list, 6, 62);
        lv_obj_set_size(list, 308, 154);
        lv_obj_set_style_pad_row(list, 6, 0);
        lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_scroll_dir(list, LV_DIR_VER);
    }
    if (lv_screen_active() != screen)
        previous = lv_screen_active();
    lv_label_set_text(title, fieldText("Home chats", "Chat in Home", "Chats en inicio"));
    lv_label_set_text(doneLabel, fieldText("Done", "Fatto", "Listo"));
    buildList();
    lv_screen_load(screen);
}
