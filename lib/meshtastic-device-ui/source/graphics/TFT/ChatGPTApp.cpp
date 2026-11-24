#include "graphics/view/TFT/TuiStatusBar.h"
#include "lvgl.h"
#include "util/ChatGPTPlan.h"
#include <cstring>

namespace
{
lv_obj_t *screen = nullptr, *previous = nullptr, *question = nullptr, *answer = nullptr, *status = nullptr;
lv_obj_t *configPanel = nullptr, *accountLabel = nullptr, *modelLabel = nullptr, *configStatus = nullptr;
int lastState = -1;
bool lastReady = false;

lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y, int width)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_width(l, width);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(l, lv_color_hex(0xffffff), 0);
    return l;
}
lv_obj_t *button(lv_obj_t *parent, const char *text, int x, int y, int width, lv_event_cb_t callback)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, width, 30);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, callback, LV_EVENT_CLICKED, nullptr);
    return b;
}
void send(lv_event_t *)
{
    if (!tdeckchatgpt::haveConfig()) {
        lv_label_set_text(status, "Apri Account per collegare ChatGPT.");
        return;
    }
    if (tdeckchatgpt::ask(lv_textarea_get_text(question)))
        lv_label_set_text(answer, "");
}
void updateAccount()
{
    bool connected = tdeckchatgpt::haveConfig();
    lv_label_set_text(accountLabel, connected ? tdeckchatgpt::accountName() : "Account non collegato");
    lv_label_set_text_fmt(modelLabel, "Modello: %s", connected ? tdeckchatgpt::modelName() : "scelto dopo l'accesso");
}
void openConfig(lv_event_t *)
{
    updateAccount();
    lv_obj_remove_flag(configPanel, LV_OBJ_FLAG_HIDDEN);
}
void tick(lv_timer_t *)
{
    if (!screen || lv_screen_active() != screen)
        return;
    int state = tdeckchatgpt::state();
    bool connected = tdeckchatgpt::haveConfig();
    if (state != lastState || connected != lastReady) {
        lastState = state;
        lastReady = connected;
        lv_label_set_text(status, state == 0 ? (connected ? "Pronto. Usa il tuo piano ChatGPT." : "Collega ChatGPT da Account.")
                                             : tdeckchatgpt::statusText());
        if (state == 2)
            lv_label_set_text(answer, tdeckchatgpt::reply());
        if (state == 3) {
            lv_label_set_text(answer, tdeckchatgpt::statusText());
            lv_label_set_text(status, "Errore. Dettagli sotto.");
        }
        updateAccount();
    }
    if (lv_obj_has_flag(configPanel, LV_OBJ_FLAG_HIDDEN)) {
        lv_group_t *g = lv_group_get_default();
        if (g && lv_group_get_focused(g) != question)
            lv_group_focus_obj(question);
    }
}
} // namespace
extern "C" void chatgpt_open(void)
{
    if (!screen) {
        screen = lv_obj_create(nullptr);
        lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(screen, lv_color_hex(0x071525), 0);
        tui_statusbar_reserve(screen);
        button(screen, "Back", 4, 0, 60, [](lv_event_t *) {
            if (previous)
                lv_screen_load(previous);
        });
        label(screen, "ChatGPT", 109, 6, 115);
        button(screen, "Account", 234, 0, 82, openConfig);
        question = lv_textarea_create(screen);
        lv_obj_set_pos(question, 4, 35);
        lv_obj_set_size(question, 312, 44);
        lv_textarea_set_max_length(question, 180);
        lv_textarea_set_placeholder_text(question, "Scrivi una domanda...");
        lv_obj_set_style_anim_duration(question, 0, LV_PART_CURSOR);
        lv_anim_delete(question, nullptr);
        if (lv_group_get_default())
            lv_group_add_obj(lv_group_get_default(), question);
        lv_obj_add_event_cb(question, send, LV_EVENT_READY, nullptr);
        button(screen, "Invia", 4, 84, 150, send);
        button(screen, "Pulisci", 162, 84, 154, [](lv_event_t *) {
            if (tdeckchatgpt::state() == 1)
                return;
            tdeckchatgpt::clear();
            lv_textarea_set_text(question, "");
            lv_label_set_text(answer, "");
            lastState = -1;
        });
        status = label(screen, "", 6, 118, 308);
        lv_obj_t *box = lv_obj_create(screen);
        lv_obj_set_style_pad_all(box, 4, 0);
        lv_obj_set_scroll_dir(box, LV_DIR_VER);
        lv_obj_set_pos(box, 2, 151);
        lv_obj_set_size(box, 316, 65);
        answer = lv_label_create(box);
        lv_obj_set_width(answer, 290);
        lv_label_set_long_mode(answer, LV_LABEL_LONG_WRAP);
        lv_label_set_text(answer, "");
        configPanel = lv_obj_create(screen);
        lv_obj_set_pos(configPanel, 0, 0);
        lv_obj_set_size(configPanel, 320, 218);
        lv_obj_set_style_pad_all(configPanel, 4, 0);
        lv_obj_set_style_bg_color(configPanel, lv_color_hex(0x071525), 0);
        lv_obj_clear_flag(configPanel, LV_OBJ_FLAG_SCROLLABLE);
        label(configPanel, "Account ChatGPT", 4, 0, 304);
        accountLabel = label(configPanel, "", 4, 26, 300);
        lv_obj_set_height(accountLabel, 26);
        lv_label_set_long_mode(accountLabel, LV_LABEL_LONG_DOT);
        modelLabel = label(configPanel, "", 4, 55, 300);
        lv_obj_set_height(modelLabel, 24);
        lv_label_set_long_mode(modelLabel, LV_LABEL_LONG_DOT);
        configStatus =
            label(configPanel, "Accesso dal Mac via USB.\nPoi basta il Wi-Fi.\nUso: chatgpt.com/settings/usage", 4, 80, 300);
        button(configPanel, "Accedi con ChatGPT", 4, 145, 300, [](lv_event_t *) {
            lv_label_set_text(configStatus, "Apri il collegamento di accesso\nnel browser del Mac collegato.");
        });
        button(configPanel, "Esci", 4, 178, 145, [](lv_event_t *) { tdeckchatgpt::disconnect(); });
        button(configPanel, "Chiudi", 158, 178, 146, [](lv_event_t *) {
            lv_obj_add_flag(configPanel, LV_OBJ_FLAG_HIDDEN);
            lv_group_focus_obj(question);
            lastState = -1;
        });
        lv_obj_add_flag(configPanel, LV_OBJ_FLAG_HIDDEN);
        lv_timer_create(tick, 300, nullptr);
    }
    if (lv_screen_active() != screen)
        previous = lv_screen_active();
    lastState = -1;
    lv_screen_load(screen);
}
