/*
 * lvgl_terminal - generic scrolling log pane for LVGL ports.
 *
 * Deliberately dumb: fixed-size ring of labels, no time source, no parsing,
 * no kernel knowledge. A port logs into it and the user sees the last N lines.
 * That keeps this shared file usable from the ring-3 desktop task, the host
 * simulator, and any future board bring-up demo without pulling in nullk
 * headers.
 */
#include <stdarg.h>
#include <stdint.h>

#include "lvgl_terminal.h"

#define TERM_MAX_TERMINALS 4u
#define TERM_MAX_LINES 24u
#define TERM_LINE_MAX 128u

typedef struct {
    lv_obj_t *obj;
    lv_obj_t *box;
    lv_obj_t *title;
    lv_obj_t *labels[TERM_MAX_LINES];
    uint32_t used;
    uint32_t next;
} term_state_t;

static term_state_t g_terms[TERM_MAX_TERMINALS];

static term_state_t *term_of(lv_obj_t *obj)
{
    uint32_t i;

    for (i = 0; i < TERM_MAX_TERMINALS; i++) {
        if (g_terms[i].obj == obj) {
            return &g_terms[i];
        }
    }
    return NULL;
}

static void term_delete_cb(lv_event_t *event)
{
    term_state_t *state = term_of(lv_event_get_target_obj(event));

    if (state != NULL) {
        for (uint32_t i = 0; i < TERM_MAX_LINES; i++) {
            state->labels[i] = NULL;
        }
        state->obj = NULL;
        state->box = NULL;
        state->title = NULL;
        state->used = 0;
        state->next = 0;
    }
}

lv_obj_t *lvgl_terminal_create(lv_obj_t *parent, int32_t x, int32_t y,
                               int32_t w, int32_t h)
{
    term_state_t *state = NULL;
    lv_obj_t *pane;
    lv_obj_t *header;

    for (uint32_t i = 0; i < TERM_MAX_TERMINALS; i++) {
        if (g_terms[i].obj == NULL) {
            state = &g_terms[i];
            break;
        }
    }
    if (state == NULL) {
        return NULL;
    }

    pane = lv_obj_create(parent);
    lv_obj_set_pos(pane, x, y);
    lv_obj_set_size(pane, w, h);
    lv_obj_set_style_bg_color(pane, lv_color_hex(0x0b0d12), 0);
    lv_obj_set_style_border_color(pane, lv_color_hex(0x2b3040), 0);
    lv_obj_set_style_border_width(pane, 1, 0);
    lv_obj_set_style_radius(pane, 12, 0);
    lv_obj_set_style_pad_all(pane, 12, 0);
    lv_obj_set_style_pad_row(pane, 8, 0);
    lv_obj_set_style_text_color(pane, lv_color_hex(0xe6e9f0), 0);
    lv_obj_set_flex_flow(pane, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(pane, LV_OBJ_FLAG_SCROLLABLE);

    header = lv_obj_create(pane);
    lv_obj_set_width(header, lv_pct(100));
    lv_obj_set_height(header, 24);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    state->title = lv_label_create(header);
    lv_label_set_text(state->title, "console");
    lv_obj_set_style_text_font(state->title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(state->title, lv_color_hex(0x8a93a6), 0);
    lv_obj_align(state->title, LV_ALIGN_LEFT_MID, 0, 0);

    state->box = lv_obj_create(pane);
    lv_obj_set_width(state->box, lv_pct(100));
    lv_obj_set_flex_grow(state->box, 1);
    lv_obj_set_style_bg_opa(state->box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(state->box, 0, 0);
    lv_obj_set_style_pad_all(state->box, 0, 0);
    lv_obj_set_style_pad_row(state->box, 4, 0);
    lv_obj_set_flex_flow(state->box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(state->box, LV_SCROLLBAR_MODE_AUTO);

    state->obj = pane;
    state->used = 0;
    state->next = 0;
    lv_obj_add_event_cb(pane, term_delete_cb, LV_EVENT_DELETE, NULL);
    return pane;
}

void lvgl_terminal_set_title(lv_obj_t *terminal, const char *title)
{
    term_state_t *state = term_of(terminal);

    if (state != NULL && state->title != NULL && title != NULL) {
        lv_label_set_text(state->title, title);
    }
}

void lvgl_terminal_log_line(lv_obj_t *terminal, const char *text)
{
    term_state_t *state = term_of(terminal);
    lv_obj_t *label;
    uint32_t slot;

    if (state == NULL || state->box == NULL || text == NULL) {
        return;
    }
    if (state->used < TERM_MAX_LINES) {
        slot = state->used++;
        label = lv_label_create(state->box);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0xc3cad6), 0);
        lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
        lv_obj_set_width(label, lv_pct(100));
        state->labels[slot] = label;
    } else {
        slot = state->next;
        label = state->labels[slot];
        state->next = (state->next + 1u) % TERM_MAX_LINES;
        if (label == NULL) {
            return;
        }
    }
    lv_label_set_text(label, text);
    lv_obj_scroll_to_y(state->box, LV_COORD_MAX, LV_ANIM_OFF);
}

void lvgl_terminal_log(lv_obj_t *terminal, const char *fmt, ...)
{
    char line[TERM_LINE_MAX];
    va_list args;

    if (terminal == NULL || fmt == NULL) {
        return;
    }
    va_start(args, fmt);
    lv_vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    lvgl_terminal_log_line(terminal, line);
}

void lvgl_terminal_clear(lv_obj_t *terminal)
{
    term_state_t *state = term_of(terminal);

    if (state == NULL || state->box == NULL) {
        return;
    }
    for (uint32_t i = 0; i < TERM_MAX_LINES; i++) {
        if (state->labels[i] != NULL) {
            lv_obj_delete(state->labels[i]);
            state->labels[i] = NULL;
        }
    }
    state->used = 0;
    state->next = 0;
}
