#include "graphics/view/TFT/TuiStatusBar.h"
#include "util/FieldLocale.h"
#include "util/WeatherData.h"
#include <cstdio>
#include <lvgl.h>

extern volatile bool tdeck_back_request;

namespace
{
lv_obj_t *screen = nullptr, *previous = nullptr, *content = nullptr, *statusLabel = nullptr, *backButton = nullptr;
lv_obj_t *cityPanel = nullptr, *cityInput = nullptr, *cityResults = nullptr, *cityStatus = nullptr;
unsigned seen = ~0u;

void closeCity()
{
    lv_obj_add_flag(cityPanel, LV_OBJ_FLAG_HIDDEN);
    lv_group_remove_obj(cityInput);
    if (lv_group_get_default())
        lv_group_focus_obj(backButton);
}

lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y, int width)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_width(l, width);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(l, lv_color_hex(0xf2f5fa), 0);
    lv_label_set_text(l, text);
    return l;
}

lv_obj_t *button(lv_obj_t *parent, const char *text, int x, int y, int width, lv_event_cb_t callback, void *data = nullptr)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, width, 28);
    lv_obj_set_style_pad_all(b, 4, 0);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_t *l = lv_label_create(b);
    lv_obj_set_style_text_font(l, &ui_font_montserrat_12, 0);
    lv_label_set_text(l, text);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, callback, LV_EVENT_CLICKED, data);
    return b;
}

void drawForecast()
{
    lv_obj_clean(content);
    const auto *f = meshweather::forecast();
    if (!f) {
        auto *l = label(content,
                        fieldText("Choose a city or refresh Zaragoza.\n\nUses connected Wi-Fi. GPS is not sent.",
                                  "Scegli una città o aggiorna Zaragoza.\n\nUsa il Wi-Fi connesso. Il GPS non viene inviato.",
                                  "Elige una ciudad o actualiza Zaragoza.\n\nUsa el Wi-Fi conectado. No se envía el GPS."),
                        6, 6, 286);
        lv_obj_set_style_text_font(l, &ui_font_montserrat_14, 0);
        return;
    }
    char text[160];
    snprintf(text, sizeof(text), "%s, %s", f->place.name, f->place.country);
    auto *place = label(content, text, 6, 0, 286);
    lv_label_set_long_mode(place, LV_LABEL_LONG_DOT);
    lv_obj_set_height(place, 22);
    lv_obj_set_style_text_color(place, lv_color_hex(0x5ac8fa), 0);
    snprintf(text, sizeof(text), "%.1f °C", f->temperature);
    auto *temperature = label(content, text, 6, 25, 130);
    lv_obj_set_style_text_font(temperature, &ui_font_montserrat_20, 0);
    auto *condition = label(content, meshweather::description(f->code), 144, 27, 148);
    lv_obj_set_style_text_font(condition, &ui_font_montserrat_14, 0);
    snprintf(text, sizeof(text),
             fieldText("Feels like %.1f °C   Humidity %d%%\nWind %.1f km/h", "Percepita %.1f °C   Umidità %d%%\nVento %.1f km/h",
                       "Sensación %.1f °C   Humedad %d%%\nViento %.1f km/h"),
             f->apparent, f->humidity, f->wind);
    auto *metrics = label(content, text, 6, 56, 286);
    lv_obj_set_style_text_font(metrics, &ui_font_montserrat_12, 0);
    auto *heading =
        label(content,
              fieldText("FORECAST | min / max | rain", "PREVISIONI | min / max | pioggia", "PRONÓSTICO | mín. / máx. | lluvia"),
              6, 94, 286);
    lv_obj_set_style_text_font(heading, &ui_font_montserrat_12, 0);
    lv_obj_set_style_text_color(heading, lv_color_hex(0x8e8e93), 0);
    for (unsigned i = 0; i < f->count; ++i) {
        const auto &d = f->days[i];
        char rain[12] = "n/d";
        if (d.rain >= 0)
            snprintf(rain, sizeof(rain), "%d%%", d.rain);
        snprintf(text, sizeof(text), "%.2s/%.2s   %.0f / %.0f °C   %s\n%s", d.date + 8, d.date + 5, d.low, d.high, rain,
                 meshweather::description(d.code));
        auto *l = label(content, text, 6, 116 + i * 44, 286);
        lv_obj_set_style_text_font(l, &ui_font_montserrat_12, 0);
    }
}

void search(lv_event_t *)
{
    if (!meshweather::search(lv_textarea_get_text(cityInput)) && !meshweather::busy())
        lv_label_set_text(cityStatus, fieldText("Enter two or more characters and connect Wi-Fi.",
                                                "Scrivi almeno due caratteri e connetti il Wi-Fi.",
                                                "Escribe al menos dos caracteres y conecta el Wi-Fi."));
}

void drawCities()
{
    lv_obj_clean(cityResults);
    for (unsigned i = 0; i < meshweather::placeCount(); ++i) {
        const auto *p = meshweather::place(i);
        char text[120];
        snprintf(text, sizeof(text), "%s, %s\n%s", p->name, p->country, p->region);
        auto *b = button(
            cityResults, text, 0, i * 48, 290,
            [](lv_event_t *e) {
                if (meshweather::choose(static_cast<unsigned>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)))))
                    closeCity();
            },
            reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
        lv_obj_set_height(b, 44);
        auto *title = lv_obj_get_child(b, 0);
        lv_obj_set_size(title, 278, 36);
        lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    }
}

void tick(lv_timer_t *)
{
    if (!screen || lv_screen_active() != screen)
        return;
    meshweather::service();
    if (seen != meshweather::generation()) {
        seen = meshweather::generation();
        char status[160];
        snprintf(status, sizeof(status), "Open-Meteo.com\n%s", meshweather::status());
        lv_label_set_text(statusLabel, status);
        lv_label_set_text(cityStatus, meshweather::status());
        drawForecast();
        drawCities();
    }
    if (!lv_obj_has_flag(cityPanel, LV_OBJ_FLAG_HIDDEN) && !meshweather::placeCount()) {
        auto *g = lv_group_get_default();
        if (g && lv_group_get_focused(g) != cityInput)
            lv_group_focus_obj(cityInput);
    }
}
} // namespace

extern "C" bool weather_back(void)
{
    if (!screen || lv_screen_active() != screen)
        return false;
    if (!lv_obj_has_flag(cityPanel, LV_OBJ_FLAG_HIDDEN)) {
        closeCity();
        return true;
    }
    if (previous && lv_obj_is_valid(previous)) {
        lv_screen_load(previous);
        return true;
    }
    return false;
}

extern "C" void weather_open(void)
{
    meshweather::init();
    if (!screen) {
        screen = lv_obj_create(nullptr);
        lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(screen, lv_color_hex(0x071525), 0);
        tui_statusbar_reserve(screen);
        backButton =
            button(screen, fieldText("Back", "Indietro", "Atrás"), 4, 0, 58, [](lv_event_t *) { tdeck_back_request = true; });
        label(screen, fieldText("Weather", "Meteo", "Tiempo"), 83, 5, 84);
        button(screen, fieldText("City", "Città", "Ciudad"), 172, 0, 62, [](lv_event_t *) {
            lv_obj_remove_flag(cityPanel, LV_OBJ_FLAG_HIDDEN);
            lv_group_t *g = lv_group_get_default();
            if (g) {
                lv_group_add_obj(g, cityInput);
                lv_group_focus_obj(cityInput);
            }
        });
        button(screen, fieldText("Refresh", "Aggiorna", "Actualizar"), 240, 0, 76, [](lv_event_t *) { meshweather::refresh(); });
        content = lv_obj_create(screen);
        lv_obj_set_pos(content, 2, 34);
        lv_obj_set_size(content, 316, 136);
        lv_obj_set_style_pad_all(content, 4, 0);
        lv_obj_set_style_bg_color(content, lv_color_hex(0x0b1e31), 0);
        lv_obj_set_scroll_dir(content, LV_DIR_VER);
        statusLabel = label(screen, "", 6, 174, 308);
        lv_obj_set_height(statusLabel, 42);
        lv_label_set_long_mode(statusLabel, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(statusLabel, &ui_font_montserrat_12, 0);
        lv_obj_set_style_text_color(statusLabel, lv_color_hex(0x8e8e93), 0);
        cityPanel = lv_obj_create(screen);
        lv_obj_set_pos(cityPanel, 0, 0);
        lv_obj_set_size(cityPanel, 320, 218);
        lv_obj_set_style_pad_all(cityPanel, 4, 0);
        lv_obj_set_style_bg_color(cityPanel, lv_color_hex(0x071525), 0);
        lv_obj_clear_flag(cityPanel, LV_OBJ_FLAG_SCROLLABLE);
        button(cityPanel, fieldText("Close", "Chiudi", "Cerrar"), 0, 0, 64, [](lv_event_t *) { closeCity(); });
        cityInput = lv_textarea_create(cityPanel);
        lv_obj_set_pos(cityInput, 0, 34);
        lv_obj_set_size(cityInput, 224, 34);
        lv_textarea_set_one_line(cityInput, true);
        lv_textarea_set_max_length(cityInput, 40);
        lv_textarea_set_placeholder_text(cityInput, fieldText("City, country...", "Città, paese...", "Ciudad, país..."));
        lv_obj_set_style_anim_duration(cityInput, 0, LV_PART_CURSOR);
        lv_obj_add_event_cb(cityInput, search, LV_EVENT_READY, nullptr);
        button(cityPanel, fieldText("Search", "Cerca", "Buscar"), 230, 36, 72, search);
        cityStatus = label(cityPanel,
                           fieldText("Search for a city. GPS is not sent.", "Cerca una città. Il GPS non viene inviato.",
                                     "Busca una ciudad. No se envía el GPS."),
                           0, 76, 300);
        lv_obj_set_height(cityStatus, 28);
        lv_label_set_long_mode(cityStatus, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(cityStatus, &ui_font_montserrat_12, 0);
        cityResults = lv_obj_create(cityPanel);
        lv_obj_set_pos(cityResults, 0, 108);
        lv_obj_set_size(cityResults, 308, 100);
        lv_obj_set_style_pad_all(cityResults, 4, 0);
        lv_obj_set_scroll_dir(cityResults, LV_DIR_VER);
        lv_obj_add_flag(cityPanel, LV_OBJ_FLAG_HIDDEN);
        lv_timer_create(tick, 500, nullptr);
    }
    if (lv_screen_active() != screen)
        previous = lv_screen_active();
    closeCity();
    seen = ~0u;
    lv_screen_load(screen);
    meshweather::refresh(false);
    tick(nullptr);
}
