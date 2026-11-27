#pragma once

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <lvgl.h>

namespace meshdeck
{
enum class LauncherIcon : uintptr_t { Mesh = 1, Maps, Calculator, ChatGPT, Repeaters, Settings, Updates, Weather };

namespace launcher_icons
{
struct Canvas {
    lv_layer_t *layer;
    int32_t x, y;

    void line(int x1, int y1, int x2, int y2, uint32_t color, int width = 3) const
    {
        lv_draw_line_dsc_t d;
        lv_draw_line_dsc_init(&d);
        d.p1 = {x + x1, y + y1};
        d.p2 = {x + x2, y + y2};
        d.color = lv_color_hex(color);
        d.width = width;
        d.round_start = d.round_end = true;
        lv_draw_line(layer, &d);
    }

    void path(std::initializer_list<lv_point_t> points, uint32_t color, int width = 3) const
    {
        auto previous = points.begin();
        for (auto next = previous + 1; next != points.end(); ++next) {
            line(previous->x, previous->y, next->x, next->y, color, width);
            previous = next;
        }
    }

    void box(int left, int top, int width, int height, uint32_t color, int radius, bool outline = false) const
    {
        lv_draw_rect_dsc_t d;
        lv_draw_rect_dsc_init(&d);
        d.radius = radius;
        d.bg_color = lv_color_hex(color);
        if (outline) {
            d.bg_opa = LV_OPA_TRANSP;
            d.border_color = lv_color_hex(color);
            d.border_width = 2;
        }
        lv_area_t area = {x + left, y + top, x + left + width - 1, y + top + height - 1};
        lv_draw_rect(layer, &d, &area);
    }

    void arc(int cx, int cy, int radius, int start, int end, uint32_t color, int width = 3) const
    {
        lv_draw_arc_dsc_t d;
        lv_draw_arc_dsc_init(&d);
        d.center = {x + cx, y + cy};
        d.radius = radius;
        d.start_angle = start;
        d.end_angle = end;
        d.width = width;
        d.rounded = true;
        d.color = lv_color_hex(color);
        lv_draw_arc(layer, &d);
    }
};

inline void draw(lv_event_t *e)
{
    lv_obj_t *object = static_cast<lv_obj_t *>(lv_event_get_target(e));
    lv_area_t area;
    lv_obj_get_coords(object, &area);
    Canvas c{lv_event_get_layer(e), area.x1, area.y1};
    const auto icon = static_cast<LauncherIcon>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
    const uint32_t accent = lv_color_to_u32(lv_obj_get_style_text_color(object, LV_PART_MAIN));
    constexpr uint32_t light = 0xf2f5fa;
    constexpr uint32_t dark = 0x18202a;

    switch (icon) {
    case LauncherIcon::Mesh:
        c.path({{12, 27}, {23, 9}, {35, 27}, {12, 27}}, accent, 2);
        c.line(23, 9, 23, 24, light, 2);
        c.box(17, 3, 12, 12, accent, LV_RADIUS_CIRCLE);
        c.box(5, 23, 12, 12, accent, LV_RADIUS_CIRCLE);
        c.box(29, 23, 12, 12, accent, LV_RADIUS_CIRCLE);
        c.box(20, 6, 6, 6, light, LV_RADIUS_CIRCLE);
        c.box(8, 26, 6, 6, light, LV_RADIUS_CIRCLE);
        c.box(32, 26, 6, 6, light, LV_RADIUS_CIRCLE);
        break;
    case LauncherIcon::Maps:
        c.path({{5, 9}, {16, 5}, {28, 9}, {40, 5}, {40, 31}, {28, 35}, {16, 31}, {5, 35}, {5, 9}}, accent, 2);
        c.line(16, 6, 16, 30, accent, 2);
        c.line(28, 10, 28, 34, accent, 2);
        c.path({{9, 25}, {19, 21}, {24, 24}}, light, 2);
        c.arc(30, 13, 8, 150, 390, light, 3);
        c.path({{24, 17}, {30, 25}, {36, 17}}, light, 3);
        c.box(28, 11, 4, 4, accent, LV_RADIUS_CIRCLE);
        break;
    case LauncherIcon::Calculator:
        c.box(10, 2, 27, 36, accent, 6, true);
        c.box(15, 7, 17, 7, accent, 2);
        c.line(27, 10, 29, 10, dark, 2);
        c.line(16, 20, 21, 20, light, 2);
        c.line(18, 18, 18, 22, light, 2);
        c.line(27, 20, 31, 20, light, 2);
        c.line(16, 28, 20, 32, light, 2);
        c.line(20, 28, 16, 32, light, 2);
        c.line(27, 28, 31, 28, accent, 2);
        c.line(27, 32, 31, 32, accent, 2);
        break;
    case LauncherIcon::ChatGPT:
        c.box(5, 5, 36, 27, accent, 9);
        c.path({{11, 29}, {11, 36}, {20, 30}}, accent, 3);
        c.box(13, 16, 4, 4, light, LV_RADIUS_CIRCLE);
        c.box(21, 16, 4, 4, light, LV_RADIUS_CIRCLE);
        c.box(29, 16, 4, 4, light, LV_RADIUS_CIRCLE);
        break;
    case LauncherIcon::Repeaters:
        c.arc(23, 14, 20, 145, 215, accent, 2);
        c.arc(23, 14, 20, 325, 395, accent, 2);
        c.arc(23, 14, 13, 140, 220, light, 2);
        c.arc(23, 14, 13, 320, 400, light, 2);
        c.box(19, 10, 8, 8, accent, LV_RADIUS_CIRCLE);
        c.path({{23, 18}, {15, 36}, {31, 36}, {23, 18}}, light, 2);
        c.line(19, 28, 27, 28, accent, 2);
        break;
    case LauncherIcon::Settings:
        c.path({{19, 3},  {27, 3},  {28, 8},  {32, 10}, {37, 8},  {41, 15}, {37, 19}, {37, 23}, {41, 27},
                {37, 34}, {32, 32}, {28, 34}, {27, 38}, {19, 38}, {18, 34}, {14, 32}, {9, 34},  {5, 27},
                {9, 23},  {9, 19},  {5, 15},  {9, 8},   {14, 10}, {18, 8},  {19, 3}},
               accent, 2);
        c.arc(23, 21, 8, 0, 360, light, 3);
        break;
    case LauncherIcon::Updates:
        c.arc(23, 20, 17, 145, 315, accent, 3);
        c.arc(23, 20, 17, 325, 495, light, 3);
        c.path({{30, 7}, {36, 8}, {37, 2}}, accent, 3);
        c.path({{9, 37}, {9, 31}, {3, 30}}, light, 3);
        c.line(23, 11, 23, 28, accent, 3);
        c.path({{17, 22}, {23, 28}, {29, 22}}, accent, 3);
        break;
    case LauncherIcon::Weather:
        c.box(8, 5, 15, 15, accent, LV_RADIUS_CIRCLE);
        c.line(15, 1, 15, 3, accent, 2);
        c.line(3, 12, 5, 12, accent, 2);
        c.line(5, 3, 7, 5, accent, 2);
        c.line(24, 3, 22, 5, accent, 2);
        c.box(12, 21, 29, 13, light, 6);
        c.box(16, 15, 16, 17, light, LV_RADIUS_CIRCLE);
        c.box(28, 19, 13, 13, light, LV_RADIUS_CIRCLE);
        break;
    }
}
} // namespace launcher_icons

inline bool drawLauncherIcon(lv_obj_t *canvas, const char *name, uint32_t accent)
{
    struct Entry {
        const char *name;
        LauncherIcon icon;
    };
    static constexpr Entry entries[] = {
        {"Mesh", LauncherIcon::Mesh},
        {"Maps", LauncherIcon::Maps},
        {"Calculator", LauncherIcon::Calculator},
        {"ChatGPT", LauncherIcon::ChatGPT},
        {"Ripetitori", LauncherIcon::Repeaters},
        {"Settings", LauncherIcon::Settings},
        {"Updates", LauncherIcon::Updates},
        {"Meteo", LauncherIcon::Weather},
    };
    for (const auto &entry : entries) {
        if (std::strcmp(name, entry.name))
            continue;
        // Draw on the existing icon canvas: no child widgets or decoded image buffers.
        lv_obj_set_style_text_color(canvas, lv_color_hex(accent), LV_PART_MAIN);
        lv_obj_add_event_cb(canvas, launcher_icons::draw, LV_EVENT_DRAW_MAIN,
                            reinterpret_cast<void *>(static_cast<uintptr_t>(entry.icon)));
        return true;
    }
    return false;
}
} // namespace meshdeck
