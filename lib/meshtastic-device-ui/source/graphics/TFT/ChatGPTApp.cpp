#include "graphics/view/TFT/TuiStatusBar.h"
#include "lvgl.h"
#include <cstdio>
#include <cstring>

namespace tdeckchatgpt
{
int state();
bool haveConfig();
int configurationResult();
const char *reply();
const char *statusText();
const char *modelName();
bool ask(const char *);
bool saveConfig(const char *, const char *);
void clear();
} // namespace tdeckchatgpt

namespace
{
lv_obj_t *screen = nullptr, *previous = nullptr, *question = nullptr, *answer = nullptr, *status = nullptr;
lv_obj_t *configPanel = nullptr, *keyArea = nullptr, *modelArea = nullptr, *configStatus = nullptr;
lv_timer_t *timer = nullptr;
int lastState = -1, lastConfig = -9;

lv_obj_t *button(lv_obj_t *parent, const char *text, int x, int y, int w, lv_event_cb_t callback)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, 30);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, callback, LV_EVENT_CLICKED, nullptr);
    return b;
}

void send(lv_event_t *)
{
    if (!tdeckchatgpt::haveConfig()) {
        lv_label_set_text(status, "Apri Config e inserisci una chiave API.");
        return;
    }
    if (tdeckchatgpt::ask(lv_textarea_get_text(question)))
        lv_label_set_text(answer, "");
}

void save(lv_event_t *)
{
    if (tdeckchatgpt::saveConfig(lv_textarea_get_text(keyArea), lv_textarea_get_text(modelArea))) {
        lv_textarea_set_text(keyArea, "");
        lv_label_set_text(configStatus, "Salvataggio...");
    } else {
        lv_label_set_text(configStatus, "Controlla chiave e modello. Attendi se occupato.");
    }
}

void openConfig(lv_event_t *)
{
    if (tdeckchatgpt::state() == 1 || tdeckchatgpt::configurationResult() == 1)
        return;
    lv_textarea_set_text(keyArea, "");
    lv_textarea_set_placeholder_text(keyArea, tdeckchatgpt::haveConfig() ? "Vuoto = mantieni la chiave salvata"
                                                                         : "Incolla o digita la chiave API");
    lv_textarea_set_text(modelArea, tdeckchatgpt::modelName());
    lv_label_set_text(configStatus, "platform.openai.com/api-keys\nWi-Fi con Internet. Uso API a consumo.");
    lv_obj_remove_flag(configPanel, LV_OBJ_FLAG_HIDDEN);
    lv_group_focus_obj(keyArea);
}

void tick(lv_timer_t *)
{
    if (!screen || lv_screen_active() != screen)
        return;
    int st = tdeckchatgpt::state();
    if (st != lastState) {
        lastState = st;
        lv_label_set_text(status, st == 0
                                      ? (tdeckchatgpt::haveConfig() ? "Pronto. Richiede Internet." : "Senza chiave. Apri Config.")
                                      : tdeckchatgpt::statusText());
        if (st == 2)
            lv_label_set_text(answer, tdeckchatgpt::reply());
        if (st == 3) {
            lv_label_set_text(answer, tdeckchatgpt::statusText());
            lv_label_set_text(status, "Errore. Dettagli sotto.");
        }
    }
    int cfg = tdeckchatgpt::configurationResult();
    if (cfg != lastConfig) {
        lastConfig = cfg;
        if (cfg == 2)
            lv_label_set_text(configStatus, "Salvato sul dispositivo. La chiave non viene mostrata.");
        if (cfg < 0)
            lv_label_set_text(configStatus, "Salvataggio non riuscito. Riprova.");
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
        lv_obj_t *title = lv_label_create(screen);
        lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
        lv_label_set_text(title, "ChatGPT");
        lv_obj_set_pos(title, 114, 6);
        button(screen, "Config", 240, 0, 76, openConfig);
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
        status = lv_label_create(screen);
        lv_obj_set_style_text_color(status, lv_color_hex(0xffffff), 0);
        lv_obj_set_pos(status, 6, 118);
        lv_obj_set_width(status, 308);
        lv_obj_set_style_text_font(status, LV_FONT_DEFAULT, 0);
        lv_label_set_long_mode(status, LV_LABEL_LONG_WRAP);
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
        lv_obj_t *keyLabel = lv_label_create(configPanel);
        lv_obj_set_style_text_color(keyLabel, lv_color_hex(0xffffff), 0);
        lv_label_set_text(keyLabel, "Chiave OpenAI API (nascosta)");
        lv_obj_set_pos(keyLabel, 4, 2);
        keyArea = lv_textarea_create(configPanel);
        lv_obj_set_pos(keyArea, 2, 24);
        lv_obj_set_size(keyArea, 304, 36);
        lv_textarea_set_one_line(keyArea, true);
        lv_textarea_set_max_length(keyArea, 511);
        lv_textarea_set_password_mode(keyArea, true);
        lv_textarea_set_password_show_time(keyArea, 0);
        lv_obj_set_style_anim_duration(keyArea, 0, LV_PART_CURSOR);
        lv_anim_delete(keyArea, nullptr);
        lv_obj_t *modelLabel = lv_label_create(configPanel);
        lv_obj_set_style_text_color(modelLabel, lv_color_hex(0xffffff), 0);
        lv_label_set_text(modelLabel, "Modello");
        lv_obj_set_pos(modelLabel, 4, 65);
        modelArea = lv_textarea_create(configPanel);
        lv_obj_set_pos(modelArea, 2, 87);
        lv_obj_set_size(modelArea, 304, 36);
        lv_textarea_set_one_line(modelArea, true);
        lv_textarea_set_max_length(modelArea, 63);
        if (lv_group_get_default()) {
            lv_group_add_obj(lv_group_get_default(), keyArea);
            lv_group_add_obj(lv_group_get_default(), modelArea);
        }
        configStatus = lv_label_create(configPanel);
        lv_obj_set_style_text_color(configStatus, lv_color_hex(0xffffff), 0);
        lv_obj_set_pos(configStatus, 4, 127);
        lv_obj_set_width(configStatus, 300);
        lv_label_set_long_mode(configStatus, LV_LABEL_LONG_WRAP);
        button(configPanel, "Salva", 4, 174, 145, save);
        button(configPanel, "Chiudi", 158, 174, 146, [](lv_event_t *) {
            lv_textarea_set_text(keyArea, "");
            lv_obj_add_flag(configPanel, LV_OBJ_FLAG_HIDDEN);
            lv_group_focus_obj(question);
            lastState = -1;
        });
        lv_obj_add_flag(configPanel, LV_OBJ_FLAG_HIDDEN);
        timer = lv_timer_create(tick, 300, nullptr);
    }
    if (lv_screen_active() != screen)
        previous = lv_screen_active();
    lastState = -1;
    lastConfig = -9;
    lv_screen_load(screen);
}
