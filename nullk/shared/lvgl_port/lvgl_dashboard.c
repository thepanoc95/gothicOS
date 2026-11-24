/*
 * lvgl_dashboard - gothicOS shell surface in LVGL.
 *
 * Geometry is computed from the active screen so the same code renders on the
 * 1280x800 simulator and on a 1024x768 QEMU framebuffer. Nothing here knows
 * where the pixels go.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "lvgl_dashboard.h"
#include "lvgl_terminal.h"

#define DASH_MARGIN 24
#define DASH_GAP 18
#define DASH_CARD_H 108
#define DASH_HEADER_H 72
#define DASH_FOOTER_H 36
#define DASH_TABS_H 48

static lv_obj_t *g_input_label;
static lv_obj_t *g_tab_buttons[2];
static lv_obj_t *g_page_dash;
static lv_obj_t *g_page_term;
static lv_obj_t *g_terminal;

typedef struct {
    lv_obj_t *root;
    lv_obj_t *screen;
    lv_obj_t *page_term;
    int32_t bar_h;
    int32_t width;
    int32_t height;
    int32_t header_h;
    int32_t footer_y;
    int32_t content_x;
    int32_t content_w;
    int32_t card_w;
    int32_t cards_y;
    int32_t section_y;
    int32_t section_h;
    int32_t chart_w;
    int32_t panel_w;
    int32_t table_y;
    int32_t table_h;
} dash_layout_t;

/* Visible keyboard focus: the keypad device moves this outline with TAB and
 * the arrow keys, so a port can see that key events reached LVGL. */
static void style_focusable(lv_obj_t *obj)
{
    lv_obj_set_style_outline_color(obj, lv_color_hex(0xd8b4fe), LV_STATE_FOCUSED);
    lv_obj_set_style_outline_width(obj, 2, LV_STATE_FOCUSED);
    lv_obj_set_style_outline_pad(obj, 2, LV_STATE_FOCUSED);
}

static void style_panel(lv_obj_t *obj, int32_t x, int32_t y, int32_t w, int32_t h)
{
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x1b1e26), 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(0x2b3040), 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_radius(obj, 14, 0);
    lv_obj_set_style_pad_all(obj, 16, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(0xe6e9f0), 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *panel(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h)
{
    lv_obj_t *obj = lv_obj_create(parent);

    style_panel(obj, x, y, w, h);
    return obj;
}

static void stat_card(lv_obj_t *parent, int32_t x, int32_t y, int32_t w,
                      const char *title, const char *value, uint32_t accent)
{
    lv_obj_t *card = panel(parent, x, y, w, DASH_CARD_H);
    lv_obj_t *label;
    lv_obj_t *text;

    lv_obj_set_style_pad_all(card, 16, 0);

    label = lv_label_create(card);
    lv_label_set_text(label, title);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0x8a93a6), 0);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, 0);

    text = lv_label_create(card);
    lv_label_set_text(text, value);
    lv_obj_set_style_text_font(text, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(text, lv_color_hex(accent), 0);
    lv_obj_align(text, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

static void build_header(const dash_layout_t *l)
{
    lv_obj_t *header = lv_obj_create(l->screen);
    lv_obj_t *brand;
    lv_obj_t *sub;
    char text[96];

    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_size(header, l->width, l->bar_h);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x12151c), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    brand = lv_label_create(header);
    lv_label_set_text(brand, "gothicOS");
    lv_obj_set_style_text_font(brand, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(brand, lv_color_hex(0xd8b4fe), 0);
    lv_obj_align(brand, LV_ALIGN_LEFT_MID, 24, 0);

    lv_snprintf(text, sizeof(text), "LVGL 9  |  %dx%d  |  null_kernel",
                (int)l->width, (int)l->height);
    sub = lv_label_create(header);
    lv_label_set_text(sub, text);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(sub, lv_color_hex(0x707a8c), 0);
    lv_obj_align(sub, LV_ALIGN_LEFT_MID, 150, 2);
}

static void build_nav(const dash_layout_t *l)
{
    static const char *const items[] = {
        "Dashboard", "Abilities", "Soft bus", "Devices", "Settings"
    };
    lv_obj_t *nav = lv_obj_create(l->screen);
    int32_t nav_w = l->content_x - DASH_MARGIN;

    lv_obj_set_pos(nav, 0, l->header_h);
    lv_obj_set_size(nav, nav_w, l->footer_y - l->header_h);
    lv_obj_set_style_bg_color(nav, lv_color_hex(0x101319), 0);
    lv_obj_set_style_border_width(nav, 0, 0);
    lv_obj_set_style_radius(nav, 0, 0);
    lv_obj_set_style_pad_all(nav, 16, 0);
    lv_obj_set_style_pad_row(nav, 10, 0);
    lv_obj_set_flex_flow(nav, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(nav, LV_OBJ_FLAG_SCROLLABLE);

    for (unsigned i = 0; i < 5; i++) {
        lv_obj_t *button = lv_button_create(nav);
        lv_obj_t *label;

        lv_obj_set_size(button, nav_w - 32, 44);
        lv_obj_set_style_radius(button, 10, 0);
        lv_obj_set_style_bg_color(button, lv_color_hex(i == 0 ? 0x2a2140 : 0x171b23), 0);
        lv_obj_set_style_border_color(button, lv_color_hex(i == 0 ? 0x6d4aa8 : 0x232936), 0);
        lv_obj_set_style_border_width(button, 1, 0);
        lv_obj_set_style_shadow_width(button, 0, 0);

        label = lv_label_create(button);
        lv_label_set_text(label, items[i]);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(i == 0 ? 0xe9d5ff : 0xa9b2c3), 0);
        lv_obj_center(label);
        /* Keyboard focus: TAB/arrows move between the nav buttons and ENTER
         * activates them, which is also how the PS/2 keypad path is proven. */
        style_focusable(button);
        lv_group_add_obj(lvgl_port_group(), button);
    }
}

static void build_chart(const dash_layout_t *l)
{
    lv_obj_t *chart = lv_chart_create(l->screen);
    lv_obj_t *title;
    lv_chart_series_t *load;
    lv_chart_series_t *ipc;

    style_panel(chart, l->content_x, l->section_y, l->chart_w, l->section_h);
    lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(chart, 0, 0);
    lv_obj_set_style_pad_all(chart, 10, 0);
    lv_obj_set_style_line_color(chart, lv_color_hex(0x232936), LV_PART_MAIN);
    lv_obj_set_style_line_width(chart, 2, LV_PART_ITEMS);

    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, 24);
    lv_chart_set_div_line_count(chart, 4, 6);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 110);

    load = lv_chart_add_series(chart, lv_color_hex(0xc084fc), LV_CHART_AXIS_PRIMARY_Y);
    ipc = lv_chart_add_series(chart, lv_color_hex(0x4b5566), LV_CHART_AXIS_PRIMARY_Y);
    for (int i = 0; i < 24; i++) {
        lv_chart_set_next_value(chart, load, 30 + (i * 7) % 55 + (i % 3) * 9);
        lv_chart_set_next_value(chart, ipc, 20 + (i * 4) % 30);
    }

    title = lv_label_create(l->screen);
    lv_label_set_text(title, "Scheduler load");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xa9b2c3), 0);
    lv_obj_set_pos(title, l->content_x, l->section_y - 24);
}

static void build_distributed(const dash_layout_t *l)
{
    lv_obj_t *distributed = panel(l->screen, l->content_x + l->chart_w + DASH_GAP,
                                  l->section_y, l->panel_w, l->section_h);
    lv_obj_t *title;
    lv_obj_t *row;
    lv_obj_t *spinner;
    lv_obj_t *text;
    int32_t inner = l->panel_w - 36;

    lv_obj_set_flex_flow(distributed, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(distributed, 12, 0);

    title = lv_label_create(distributed);
    lv_label_set_text(title, "Distributed");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);

    {
        lv_obj_t *sw = lv_switch_create(distributed);

        lv_obj_set_size(sw, 52, 28);
        lv_obj_add_state(sw, LV_STATE_CHECKED);
        style_focusable(sw);
        lv_group_add_obj(lvgl_port_group(), sw);
    }

    {
        lv_obj_t *cb = lv_checkbox_create(distributed);

        lv_checkbox_set_text(cb, "Allow remote abilities");
        lv_obj_set_style_text_font(cb, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(cb, lv_color_hex(0xa9b2c3), 0);
        lv_obj_add_state(cb, LV_STATE_CHECKED);
        style_focusable(cb);
        lv_group_add_obj(lvgl_port_group(), cb);
    }

    {
        lv_obj_t *slider = lv_slider_create(distributed);
        lv_obj_t *bar = lv_bar_create(distributed);

        lv_obj_set_width(slider, inner);
        lv_slider_set_value(slider, 62, LV_ANIM_OFF);
        lv_obj_set_size(bar, inner, 10);
        lv_bar_set_value(bar, 41, LV_ANIM_OFF);
    }

    row = lv_obj_create(distributed);
    lv_obj_set_size(row, inner, 40);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 12, 0);

    spinner = lv_spinner_create(row);
    lv_obj_set_size(spinner, 30, 30);

    text = lv_label_create(row);
    lv_label_set_text(text, "2 nodes online");
    lv_obj_set_style_text_font(text, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(text, lv_color_hex(0xa9b2c3), 0);
}

static void build_table(const dash_layout_t *l)
{
    static const char *const rows[][3] = {
        {"00:00:12", "init64", "VFS ready, shell spawned"},
        {"00:00:19", "ability", "system.softbus started"},
        {"00:01:03", "sched", "phone-b advertised 2 abilities"},
        {"00:02:41", "remote", "camera.capture -> phone-b"},
        {"00:04:07", "lvgl", "display ready, LVGL 9"}
    };
    lv_obj_t *table = lv_table_create(l->screen);
    int32_t span = l->chart_w + DASH_GAP + l->panel_w;
    int32_t col0 = span / 9;
    int32_t col1 = span / 7;

    lv_obj_set_pos(table, l->content_x, l->table_y);
    lv_obj_set_size(table, span, l->table_h);
    lv_obj_set_style_bg_color(table, lv_color_hex(0x1b1e26), 0);
    lv_obj_set_style_bg_opa(table, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(table, lv_color_hex(0x2b3040), 0);
    lv_obj_set_style_radius(table, 14, 0);
    lv_obj_set_style_pad_all(table, 10, 0);
    lv_obj_set_style_text_font(table, &lv_font_montserrat_14, LV_PART_ITEMS);
    lv_obj_set_style_text_color(table, lv_color_hex(0xc3cad6), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(table, lv_color_hex(0x1b1e26), LV_PART_ITEMS);
    lv_obj_set_style_border_color(table, lv_color_hex(0x232936), LV_PART_ITEMS);
    lv_obj_set_style_pad_ver(table, 6, LV_PART_ITEMS);
    lv_obj_set_style_pad_hor(table, 10, LV_PART_ITEMS);
    lv_obj_remove_flag(table, LV_OBJ_FLAG_SCROLLABLE);

    lv_table_set_col_cnt(table, 3);
    lv_table_set_row_cnt(table, 6);
    lv_table_set_col_width(table, 0, col0);
    lv_table_set_col_width(table, 1, col1);
    lv_table_set_col_width(table, 2, span - col0 - col1 - 40);
    lv_table_set_cell_value(table, 0, 0, "TIME");
    lv_table_set_cell_value(table, 0, 1, "SERVICE");
    lv_table_set_cell_value(table, 0, 2, "MESSAGE");
    for (uint32_t i = 0; i < 5u; i++) {
        lv_table_set_cell_value(table, i + 1u, 0, rows[i][0]);
        lv_table_set_cell_value(table, i + 1u, 1, rows[i][1]);
        lv_table_set_cell_value(table, i + 1u, 2, rows[i][2]);
    }
}

static void build_footer(const dash_layout_t *l)
{
    lv_obj_t *footer = lv_obj_create(l->root);
    lv_obj_t *status;

    lv_obj_set_pos(footer, 0, l->footer_y);
    lv_obj_set_size(footer, l->width, DASH_FOOTER_H);
    lv_obj_set_style_bg_color(footer, lv_color_hex(0x12151c), 0);
    lv_obj_set_style_border_width(footer, 0, 0);
    lv_obj_set_style_radius(footer, 0, 0);
    lv_obj_set_style_pad_all(footer, 0, 0);
    lv_obj_remove_flag(footer, LV_OBJ_FLAG_SCROLLABLE);

    status = lv_label_create(footer);
    lv_label_set_text(status,
                      "kernel: null_kernel   |   LVGL 9   |   input: PS/2   |   "
                      "abilities: 6   |   tasks: 4");
    lv_obj_set_style_text_font(status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(status, lv_color_hex(0x707a8c), 0);
    lv_obj_align(status, LV_ALIGN_LEFT_MID, 20, 0);

    g_input_label = lv_label_create(footer);
    lv_label_set_text(g_input_label, "input events: 0");
    lv_obj_set_style_text_font(g_input_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(g_input_label, lv_color_hex(0x86efac), 0);
    lv_obj_align(g_input_label, LV_ALIGN_RIGHT_MID, -20, 0);
}

static void style_tab(lv_obj_t *button, int active)
{
    lv_obj_t *label = lv_obj_get_child(button, 0);

    lv_obj_set_style_bg_color(button, lv_color_hex(active ? 0x2a2140 : 0x171b23), 0);
    lv_obj_set_style_border_color(button, lv_color_hex(active ? 0x6d4aa8 : 0x232936), 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_radius(button, 8, 0);
    if (label != NULL) {
        lv_obj_set_style_text_color(label, lv_color_hex(active ? 0xe9d5ff : 0xa9b2c3), 0);
    }
}

static void tab_event_cb(lv_event_t *event)
{
    lvgl_dashboard_show_tab((uint32_t)(uintptr_t)lv_event_get_user_data(event));
}

static void build_tabs(const dash_layout_t *l)
{
    static const char *const titles[] = {"Dashboard", "Terminal"};
    lv_obj_t *strip = lv_obj_create(l->root);

    lv_obj_set_pos(strip, 0, l->bar_h);
    lv_obj_set_size(strip, l->width, DASH_TABS_H);
    lv_obj_set_style_bg_color(strip, lv_color_hex(0x0d0f14), 0);
    lv_obj_set_style_border_width(strip, 0, 0);
    lv_obj_set_style_radius(strip, 0, 0);
    lv_obj_set_style_pad_all(strip, 0, 0);
    lv_obj_set_style_pad_left(strip, DASH_MARGIN, 0);
    lv_obj_set_style_pad_column(strip, 8, 0);
    lv_obj_set_flex_flow(strip, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(strip, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(strip, LV_OBJ_FLAG_SCROLLABLE);

    for (uint32_t i = 0; i < 2u; i++) {
        lv_obj_t *button = lv_button_create(strip);
        lv_obj_t *label;

        lv_obj_set_size(button, 118, 32);
        label = lv_label_create(button);
        lv_label_set_text(label, titles[i]);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
        lv_obj_center(label);
        style_focusable(button);
        lv_obj_add_event_cb(button, tab_event_cb, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)i);
        lv_group_add_obj(lvgl_port_group(), button);
        g_tab_buttons[i] = button;
    }
    style_tab(g_tab_buttons[0], 1);
    style_tab(g_tab_buttons[1], 0);
}

/* A page is a transparent, full-surface container: content inside it keeps the
 * same absolute coordinates as when it was drawn straight on the screen. */
static lv_obj_t *page_container(lv_obj_t *parent, int32_t w, int32_t h)
{
    lv_obj_t *page = lv_obj_create(parent);

    lv_obj_set_pos(page, 0, 0);
    lv_obj_set_size(page, w, h);
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_radius(page, 0, 0);
    lv_obj_set_style_pad_all(page, 0, 0);
    lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    return page;
}

void lvgl_dashboard_show_tab(uint32_t index)
{
    uint32_t active = (index == 1u) ? 1u : 0u;

    if (g_page_dash == NULL || g_page_term == NULL) {
        return;
    }
    if (active == 1u) {
        lv_obj_add_flag(g_page_dash, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(g_page_term, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(g_page_term, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(g_page_dash, LV_OBJ_FLAG_HIDDEN);
    }
    for (uint32_t i = 0; i < 2u; i++) {
        style_tab(g_tab_buttons[i], (int)(i == active));
    }
}

lv_obj_t *lvgl_dashboard_terminal(void)
{
    return g_terminal;
}

void lvgl_dashboard_log(const char *fmt, ...)
{
    char line[128];
    va_list args;

    if (g_terminal == NULL || fmt == NULL) {
        return;
    }
    va_start(args, fmt);
    lv_vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    lvgl_terminal_log_line(g_terminal, line);
}

void lvgl_dashboard_set_input_count(uint32_t events)
{
    char text[64];
    int32_t x = 0;
    int32_t y = 0;

    if (g_input_label == NULL) {
        return;
    }
    lvgl_port_last_pointer(&x, &y, NULL);
    lv_snprintf(text, sizeof(text), "input: %u   cursor: %d,%d",
                (unsigned)events, (int)x, (int)y);
    lv_label_set_text(g_input_label, text);
}

void lvgl_dashboard_create(void)
{
    dash_layout_t l;
    int32_t gap_total = DASH_GAP * 2;

    l.root = lv_screen_active();
    l.screen = l.root;
    l.width = lv_obj_get_width(l.root);
    l.height = lv_obj_get_height(l.root);
    if (l.width < 640 || l.height < 400) {
        /* Too small for the shell layout: draw a single readable message. */
        lv_obj_t *note = lv_label_create(l.screen);

        lv_label_set_text(note, "gothicOS: framebuffer too small for the shell");
        lv_obj_center(note);
        return;
    }

    l.bar_h = (l.height >= 700) ? DASH_HEADER_H : 56;
    l.header_h = l.bar_h + DASH_TABS_H;
    l.footer_y = l.height - DASH_FOOTER_H;
    l.content_x = (l.width >= 1200) ? 220 : 180;
    l.content_w = l.width - l.content_x - DASH_MARGIN;
    l.card_w = (l.content_w - gap_total) / 3;
    l.cards_y = l.header_h + 24;
    l.section_y = l.cards_y + DASH_CARD_H + 30;

    l.section_h = 250;
    if (l.footer_y - l.section_y < 420) {
        l.section_h = 210;
    }
    l.chart_w = (l.content_w - DASH_GAP) * 54 / 100;
    l.panel_w = l.content_w - DASH_GAP - l.chart_w;

    l.table_y = l.section_y + l.section_h + 20;
    l.table_h = l.footer_y - l.table_y - 16;

    lv_obj_set_style_bg_color(l.root, lv_color_hex(0x0d0f14), 0);
    lv_obj_set_style_text_color(l.root, lv_color_hex(0xe6e9f0), 0);

    build_header(&l);
    build_tabs(&l);

    /* Terminal page first so the dashboard page draws over it when both exist,
     * then the pages themselves decide visibility. */
    l.page_term = page_container(l.root, l.width, l.footer_y);
    g_page_term = l.page_term;
    g_page_dash = page_container(l.root, l.width, l.footer_y);
    l.screen = g_page_dash;
    build_nav(&l);
    stat_card(l.screen, l.content_x, l.cards_y, l.card_w, "Abilities", "6", 0xc084fc);
    stat_card(l.screen, l.content_x + l.card_w + DASH_GAP, l.cards_y, l.card_w,
              "Uptime", "0:04:12", 0x86efac);
    stat_card(l.screen, l.content_x + 2 * (l.card_w + DASH_GAP), l.cards_y,
              l.card_w, "Tasks", "4", 0x93c5fd);
    build_chart(&l);
    build_distributed(&l);
    if (l.table_h >= 120) {
        build_table(&l);
    }
    build_footer(&l);

    /* Below the tab strip and the header, above the footer. */
    {
        int32_t term_y = l.header_h + DASH_MARGIN;

        g_terminal = lvgl_terminal_create(l.page_term, DASH_MARGIN, term_y,
                                          l.width - 2 * DASH_MARGIN,
                                          l.footer_y - term_y - DASH_MARGIN);
    }
    lvgl_terminal_set_title(g_terminal, "console   |   null_kernel dev log");
    lvgl_dashboard_show_tab(0);
}
