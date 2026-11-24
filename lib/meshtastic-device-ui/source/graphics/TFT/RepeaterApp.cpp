#include "graphics/view/TFT/TuiStatusBar.h"
#include "lvgl.h"
#include "util/RepeaterData.h"
#include <cstdio>

namespace
{
lv_obj_t *screen = nullptr, *previous = nullptr, *list = nullptr, *statusLabel = nullptr, *referenceLabel = nullptr;
lv_obj_t *detail = nullptr;
unsigned seenGeneration = ~0u;
RepeaterRecord selected = {};
uint32_t selectedDistance = 0;

lv_obj_t *button(lv_obj_t *parent, const char *title, int x, int y, int w, lv_event_cb_t cb)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, 28);
    lv_obj_t *label = lv_label_create(b);
    lv_label_set_text(label, title);
    lv_obj_center(label);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
    return b;
}

void showDetail(void *)
{
    if (!detail) {
        detail = lv_obj_create(screen);
        lv_obj_set_pos(detail, 0, 0);
        lv_obj_set_size(detail, 320, 218);
    }
    lv_obj_clean(detail);
    lv_obj_remove_flag(detail, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_pad_all(detail, 6, 0);
    lv_obj_t *label = lv_label_create(detail);
    lv_obj_set_width(label, 294);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    int32_t khz = selected.offsetHz / 1000;
    uint32_t magnitude = khz < 0 ? (uint32_t)(-(int64_t)khz) : (uint32_t)khz;
    lv_label_set_text_fmt(
        label, "%s  %s\n%s\n%lu.%05lu MHz  %s\nShift: %s%lu kHz\nTX: %s  RX: %s\n%s\n%lu.%lu km circa\nFonte: HearHam.com",
        selected.callsign, selected.mode, selected.city, (unsigned long)(selected.frequencyHz / 1000000),
        (unsigned long)((selected.frequencyHz % 1000000) / 10), selected.operational ? "attivo" : "inattivo", khz < 0 ? "-" : "+",
        (unsigned long)magnitude, selected.encode[0] ? selected.encode : "--", selected.decode[0] ? selected.decode : "--",
        selected.restriction, (unsigned long)(selectedDistance / 1000), (unsigned long)((selectedDistance % 1000) / 100));
    button(detail, "Chiudi", 100, 174, 100, [](lv_event_t *) { lv_obj_add_flag(detail, LV_OBJ_FLAG_HIDDEN); });
}

void rebuild()
{
    lv_obj_clean(list);
    unsigned count = tdeck_repeaters_near_count();
    if (!count) {
        lv_obj_t *text = lv_label_create(list);
        lv_obj_set_width(text, 290);
        lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);
        lv_label_set_text(text, tdeck_repeaters_count()
                                    ? "In attesa del GPS. Per provare la lista scegli Zaragoza come riferimento."
                                    : "Nessun archivio sulla microSD. Con Wi-Fi collegato il download parte all'apertura. Puoi "
                                      "anche premere Aggiorna.");
        return;
    }
    for (unsigned i = 0; i < count; i++) {
        const RepeaterRecord *r = tdeck_repeaters_record(i);
        uint32_t metres = tdeck_repeaters_distance(i);
        lv_obj_t *row = lv_button_create(list);
        lv_obj_set_width(row, 296);
        lv_obj_set_height(row, 66);
        lv_obj_set_style_bg_color(row, lv_color_hex(r->operational ? 0x18314a : 0x333333), 0);
        lv_obj_t *text = lv_label_create(row);
        lv_obj_set_width(text, 278);
        lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);
        lv_label_set_text_fmt(text, "%s  %s  %lu.%lu km\n%lu.%05lu MHz  %s%s", r->callsign, r->mode,
                              (unsigned long)(metres / 1000), (unsigned long)((metres % 1000) / 100),
                              (unsigned long)(r->frequencyHz / 1000000), (unsigned long)((r->frequencyHz % 1000000) / 10),
                              r->city, r->operational ? "" : " (inattivo)");
        lv_obj_add_event_cb(
            row,
            [](lv_event_t *e) {
                unsigned index = (unsigned)(uintptr_t)lv_event_get_user_data(e);
                const RepeaterRecord *record = tdeck_repeaters_record(index);
                if (!record)
                    return;
                selected = *record;
                selectedDistance = tdeck_repeaters_distance(index);
                lv_async_call(showDetail, nullptr);
            },
            LV_EVENT_CLICKED, (void *)(uintptr_t)i);
    }
}

void tick(lv_timer_t *)
{
    if (!screen || lv_screen_active() != screen)
        return;
    lv_label_set_text(statusLabel, tdeck_repeaters_status());
    lv_label_set_text(referenceLabel, tdeck_repeaters_reference());
    if (seenGeneration != tdeck_repeaters_generation()) {
        seenGeneration = tdeck_repeaters_generation();
        rebuild();
    }
}
} // namespace

extern "C" void repeaters_open(void)
{
    tdeck_repeaters_open();
    if (!screen) {
        screen = lv_obj_create(nullptr);
        lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(screen, lv_color_hex(0x071525), 0);
        tui_statusbar_reserve(screen);
        button(screen, "Back", 4, 0, 60, [](lv_event_t *) {
            if (previous)
                lv_screen_load(previous);
        });
        button(screen, "Zaragoza", 76, 0, 100, [](lv_event_t *) { tdeck_repeaters_use_zaragoza(); });
        button(screen, "Aggiorna", 188, 0, 128, [](lv_event_t *) { tdeck_repeaters_request_update(); });
        statusLabel = lv_label_create(screen);
        lv_obj_set_style_text_color(statusLabel, lv_color_hex(0xffffff), 0);
        lv_obj_set_pos(statusLabel, 5, 33);
        lv_obj_set_width(statusLabel, 310);
        lv_obj_set_height(statusLabel, 30);
        lv_label_set_long_mode(statusLabel, LV_LABEL_LONG_DOT);
        referenceLabel = lv_label_create(screen);
        lv_obj_set_style_text_color(referenceLabel, lv_color_hex(0xffffff), 0);
        lv_obj_set_pos(referenceLabel, 5, 66);
        lv_obj_set_width(referenceLabel, 310);
        lv_obj_set_height(referenceLabel, 22);
        lv_label_set_long_mode(referenceLabel, LV_LABEL_LONG_DOT);
        list = lv_obj_create(screen);
        lv_obj_set_pos(list, 0, 90);
        lv_obj_set_size(list, 320, 126);
        lv_obj_set_style_pad_all(list, 4, 0);
        lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
        lv_timer_create(tick, 750, nullptr);
    }
    if (lv_screen_active() != screen)
        previous = lv_screen_active();
    seenGeneration = ~0u;
    lv_screen_load(screen);
    tick(nullptr);
}
