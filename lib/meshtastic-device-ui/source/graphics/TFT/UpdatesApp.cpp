#include "graphics/view/TFT/TuiStatusBar.h"
#include "lvgl.h"
#include "util/FieldUpdate.h"

namespace
{
lv_obj_t *screen = nullptr, *previous = nullptr, *status = nullptr, *progress = nullptr;
void tick(lv_timer_t *)
{
    if (!screen || lv_screen_active() != screen)
        return;
    char message[144];
    tdeck_ota_status(message, sizeof(message));
    lv_label_set_text(status, message);
    lv_bar_set_value(progress, tdeck_ota_progress(), LV_ANIM_OFF);
}
} // namespace
extern "C" void updates_open(void)
{
    if (!screen) {
        screen = lv_obj_create(nullptr);
        lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
        tui_statusbar_reserve(screen);
        lv_obj_t *title = lv_label_create(screen);
        lv_label_set_text(title, "Aggiornamenti Field");
        lv_obj_set_pos(title, 8, 4);
        lv_obj_t *version = lv_label_create(screen);
        lv_label_set_text_fmt(version, "Versione %s", kFieldVersion);
        lv_obj_set_pos(version, 8, 29);
        status = lv_label_create(screen);
        lv_obj_set_pos(status, 8, 57);
        lv_obj_set_width(status, 300);
        lv_obj_set_height(status, 58);
        lv_label_set_long_mode(status, LV_LABEL_LONG_WRAP);
        progress = lv_bar_create(screen);
        lv_obj_set_pos(progress, 12, 122);
        lv_obj_set_size(progress, 296, 12);
        lv_obj_t *autoLabel = lv_label_create(screen);
        lv_label_set_text(autoLabel, "Controllo giornaliero Wi-Fi");
        lv_obj_set_pos(autoLabel, 8, 147);
        lv_obj_t *sw = lv_switch_create(screen);
        lv_obj_set_pos(sw, 260, 141);
        if (tdeck_ota_get_auto())
            lv_obj_add_state(sw, LV_STATE_CHECKED);
        lv_obj_add_event_cb(
            sw, [](lv_event_t *e) { tdeck_ota_set_auto(lv_obj_has_state((lv_obj_t *)lv_event_get_target(e), LV_STATE_CHECKED)); },
            LV_EVENT_VALUE_CHANGED, nullptr);
        lv_obj_t *check = lv_button_create(screen);
        lv_obj_set_pos(check, 8, 180);
        lv_obj_set_size(check, 220, 30);
        lv_obj_t *checkText = lv_label_create(check);
        lv_label_set_text(checkText, "Controlla e installa");
        lv_obj_center(checkText);
        lv_obj_add_event_cb(
            check, [](lv_event_t *) { tdeck_ota_request(); }, LV_EVENT_CLICKED, nullptr);
        lv_obj_t *back = lv_button_create(screen);
        lv_obj_set_pos(back, 236, 180);
        lv_obj_set_size(back, 76, 30);
        lv_obj_t *backText = lv_label_create(back);
        lv_label_set_text(backText, "Back");
        lv_obj_center(backText);
        lv_obj_add_event_cb(
            back,
            [](lv_event_t *) {
                if (previous)
                    lv_screen_load(previous);
            },
            LV_EVENT_CLICKED, nullptr);
        lv_timer_create(tick, 500, nullptr);
    }
    if (lv_screen_active() != screen)
        previous = lv_screen_active();
    lv_screen_load(screen);
    tick(nullptr);
}
