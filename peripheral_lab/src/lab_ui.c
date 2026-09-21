/*
 * lab_ui.c — 主界面：创建外设卡片和详情页，处理边缘导航，并定时显示后台结果。
 */
#include "lab.h"
#include "lab_ui.h"
#include "lab_game.h"
#include "lab_ble.h"
#include "lab_pan.h"
#include <string.h>
#include <stdio.h>

#define BG 0x0C1220
#define PANEL 0x172235
#define MUTED 0x97A8C0
#define TEXT 0xEDF4FC
#define GREEN 0x86F5CA

static lv_obj_t *dashboard, *detail, *cards[LAB_COUNT], *card_status[LAB_COUNT];
static lv_obj_t *summary, *progress, *state_label, *value_label, *detail_label, *detail_body;
static lv_obj_t *actions[LAB_ACTION_COUNT], *confirm_row, *busy_label, *stop_button, *overlay;
static void (*overlay_on_close)(void);
static int selected = -1;
static uint32_t shown_revision = UINT32_MAX;
static uint32_t card_revision[LAB_COUNT];
static bool last_busy;
static lv_obj_t *speaker_sliders[2], *speaker_values[2];
static void refresh(lv_timer_t *timer);
static bool gesture_tracking, gesture_blocked, navigation_pending;
static lv_point_t gesture_start;
/* 仅识别边缘向内滑动；页面中央始终保留滚动和测试交互。 */
int lab_ui_swipe_direction(int x, int y, int dx, int dy)
{
    if (x <= 24 && dx >= 60 && dx > 2 * LV_ABS(dy)) return 1;
    if (y >= lv_display_get_vertical_resolution(NULL) - 24 && dy <= -70 && -dy > 2 * LV_ABS(dx)) return 2;
    return 0;
}
static void navigate(void *data);
/* 跟踪触摸起点与位移，仅将符合边缘条件的单指手势交给导航。 */
static void gesture_event(lv_event_t *e)
{
    lv_indev_t *input = lv_event_get_user_data(e);
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        lv_indev_get_point(input, &gesture_start);
        gesture_tracking = true;
        gesture_blocked = !lab_touch_gesture_allowed();
    } else if (code == LV_EVENT_RELEASED && gesture_tracking) {
        lv_point_t end;
        lv_indev_get_point(input, &end);
        gesture_tracking = false;
        int direction = lab_ui_swipe_direction(gesture_start.x, gesture_start.y,
                                               end.x - gesture_start.x, end.y - gesture_start.y);
        if (direction && !gesture_blocked && lab_touch_gesture_allowed() && !navigation_pending && (detail || overlay)) {
            navigation_pending = true;
            lv_indev_stop_processing(input);
            lv_indev_reset(input, NULL);
            lv_indev_wait_release(input);
            lv_async_call(navigate, (void *)(uintptr_t)direction);
        }
    }
}

static const char *status_name(lab_status_t s)
{
    static const char *names[] = {"NOT TESTED", "RUNNING", "PASS", "FAIL", "CHECK RESULT"};
    return names[s];
}
static uint32_t status_color(lab_status_t s)
{
    return s == LAB_PASS ? GREEN : s == LAB_FAIL ? 0xFF969D : s == LAB_RUNNING ? 0x80CCFF : s == LAB_OBSERVE ? 0xFFE38B : MUTED;
}
static lv_obj_t *box(lv_obj_t *parent, int w, int h, uint32_t color, int radius)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}
static lv_obj_t *label(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *o = lv_label_create(parent);
    lv_label_set_text(o, text);
    lv_obj_set_style_text_font(o, font, 0);
    lv_obj_set_style_text_color(o, lv_color_hex(color), 0);
    return o;
}
static lv_obj_t *button(lv_obj_t *parent, const char *text, int w, uint32_t color,
                        lv_event_cb_t cb, void *data)
{
    lv_obj_t *o = box(parent, w, 48, color, 14);
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(o, LV_OPA_70, LV_STATE_PRESSED);
    lv_obj_set_style_opa(o, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_t *l = label(o, text, &lv_font_montserrat_14, color == GREEN ? BG : TEXT);
    lv_obj_center(l);
    lv_obj_add_event_cb(o, cb, LV_EVENT_CLICKED, data);
    return o;
}
static void back_event(lv_event_t *e)
{
    (void)e;
    if (lab_continuous()) lab_stop();
    if (detail) { lv_obj_delete(detail); detail = NULL; }
    selected = -1;
    lv_obj_remove_flag(dashboard, LV_OBJ_FLAG_HIDDEN);
}
/* 在延后的界面回调中完成返回或回首页，避免在输入处理途中销毁对象。 */
static void navigate(void *data)
{
    unsigned direction = (unsigned)(uintptr_t)data;
    bool had_overlay = overlay != NULL;
    if (overlay) lab_overlay_close();
    if (direction == 2 || !had_overlay) back_event(NULL);
    navigation_pending = false;
}
static void stop_event(lv_event_t *e)
{
    (void)e;
    lab_stop();
    lv_label_set_text(busy_label, "Stopping safely after the current I/O...");
}
/* 普通动作提交给后台，显示、触摸、蓝牙和游戏动作打开各自测试层。 */
static void action_event(lv_event_t *e)
{
    if (lab_busy()) return;
    unsigned action = (unsigned)(uintptr_t)lv_event_get_user_data(e);
    if (selected == LAB_GAME) lab_game_open();
    else if (selected == LAB_PAN) lab_pan_open();
    else if (selected == LAB_BLE) lab_ble_open();
    else if (selected == LAB_DISPLAY) lab_display_open();
    else if (selected == LAB_TOUCH) { if (action == 0) lab_touch_open(); else lab_touch_multi_open(); }
    else {
        if (selected == LAB_SPEAKER && action < 8) {
            unsigned volume, frequency;
            lab_speaker_settings(&volume, &frequency);
            lab_speaker_configure(volume, lab_speaker_notes[action]);
            lv_slider_set_value(speaker_sliders[1], lab_speaker_notes[action], LV_ANIM_OFF);
            lv_label_set_text_fmt(speaker_values[1], "Frequency  %u Hz", lab_speaker_notes[action]);
        }
        lab_start((lab_id_t)selected, action);
    }
}
static void confirm_event(lv_event_t *e)
{
    lab_confirm((lab_id_t)selected, (uintptr_t)lv_event_get_user_data(e) != 0);
}
static void speaker_slider_event(lv_event_t *e)
{
    if (lab_busy()) return;
    unsigned volume = lv_slider_get_value(speaker_sliders[0]);
    unsigned frequency = lv_slider_get_value(speaker_sliders[1]);
    lab_speaker_configure(volume, frequency);
    lv_label_set_text_fmt(speaker_values[0], "Volume  %u / 15%s", volume, volume ? "" : " (mute)");
    lv_label_set_text_fmt(speaker_values[1], "Frequency  %u Hz", frequency);
    (void)e;
}
static void speaker_controls(lv_obj_t *body)
{
    unsigned volume, frequency;
    lab_speaker_settings(&volume, &frequency);
    lv_obj_t *panel = box(body, LV_PCT(100), LV_SIZE_CONTENT, PANEL, 20);
    lv_obj_set_style_pad_all(panel, 18, 0);
    lv_obj_set_style_pad_row(panel, 14, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    for (unsigned i = 0; i < 2; ++i) {
        speaker_values[i] = label(panel, "", &lv_font_montserrat_14, TEXT);
        lv_obj_t *slider = speaker_sliders[i] = lv_slider_create(panel);
        lv_obj_set_size(slider, LV_PCT(100), 10);
        lv_obj_set_style_bg_color(slider, lv_color_hex(GREEN), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(slider, lv_color_hex(GREEN), LV_PART_KNOB);
        lv_slider_set_range(slider, i ? 100 : 0, i ? 4000 : 15);
        lv_slider_set_value(slider, i ? frequency : volume, LV_ANIM_OFF);
        lv_obj_add_event_cb(slider, speaker_slider_event, LV_EVENT_VALUE_CHANGED, NULL);
    }
    speaker_slider_event(NULL);
    label(panel, "Adjust, then play (2 seconds)", &lv_font_montserrat_12, MUTED);
}
/* 根据注册表生成详情、动作按钮、操作提示和 API 摘要。 */
static void open_detail(lv_event_t *e)
{
    selected = (int)(uintptr_t)lv_event_get_user_data(e);
    const lab_module_t *m = &lab_modules[selected];
    lv_obj_add_flag(dashboard, LV_OBJ_FLAG_HIDDEN);
    detail = box(lv_screen_active(), LV_PCT(100), LV_PCT(100), BG, 0);
    lv_obj_t *back = button(detail, LV_SYMBOL_LEFT, 48, PANEL, back_event, NULL);
    lv_obj_set_pos(back, 22, 18);
    lv_obj_t *title = label(detail, m->title, &lv_font_montserrat_20, TEXT);
    lv_obj_set_pos(title, 82, 20);
    lv_obj_t *sub = label(detail, m->subtitle, &lv_font_montserrat_12, MUTED);
    lv_obj_set_pos(sub, 82, 47);
    lv_obj_t *body = detail_body = box(detail, LV_PCT(100), 360, BG, 0);
    lv_obj_set_pos(body, 0, 82);
    lv_obj_set_height(body, lv_display_get_vertical_resolution(NULL) - 94);
    lv_obj_add_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(body, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_pad_hor(body, 22, 0);
    lv_obj_set_style_pad_bottom(body, 24, 0);
    lv_obj_set_style_pad_row(body, 14, 0);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_t *result = box(body, LV_PCT(100), LV_SIZE_CONTENT, PANEL, 20);
    lv_obj_set_style_pad_all(result, 18, 0);
    lv_obj_set_style_pad_row(result, 9, 0);
    lv_obj_set_flex_flow(result, LV_FLEX_FLOW_COLUMN);
    state_label = label(result, "", &lv_font_montserrat_12, m->accent);
    value_label = label(result, "Ready to test", &lv_font_montserrat_24, TEXT);
    lv_obj_set_width(value_label, LV_PCT(100));
    detail_label = label(result, "", &lv_font_montserrat_14, MUTED);
    lv_obj_set_width(detail_label, LV_PCT(100));
    if (selected == LAB_SPEAKER) speaker_controls(body);
    lv_obj_t *action_grid = box(body, LV_PCT(100), LV_SIZE_CONTENT, BG, 0);
    lv_obj_set_flex_flow(action_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_gap(action_grid, 10, 0);
    int width = (lv_display_get_horizontal_resolution(NULL) - 54) / 2;
    for (int n = 0; n < LAB_ACTION_COUNT; ++n) {
        int i = selected == LAB_SPEAKER ? (n + 8) % LAB_ACTION_COUNT : n;
        actions[i] = NULL;
        if (m->actions[i]) actions[i] = button(action_grid, m->actions[i], width, (selected == LAB_SPEAKER ? i == 8 : i == 0) ? GREEN : PANEL, action_event, (void *)(uintptr_t)i);
    }
    busy_label = label(body, "Another test is running. Please wait.", &lv_font_montserrat_12, 0x80CCFF);
    lv_obj_set_width(busy_label, LV_PCT(100));
    stop_button = button(detail, LV_SYMBOL_STOP "  STOP TEST", lv_display_get_horizontal_resolution(NULL) - 44, 0xE84459, stop_event, NULL);
    lv_obj_set_height(stop_button, 58);
    lv_obj_set_pos(stop_button, 22, lv_display_get_vertical_resolution(NULL) - 72);
    lv_obj_set_style_border_width(stop_button, 2, 0);
    lv_obj_set_style_border_color(stop_button, lv_color_hex(0xFFABB6), 0);
    lv_obj_set_style_text_font(lv_obj_get_child(stop_button, 0), &lv_font_montserrat_18, 0);
    confirm_row = box(body, LV_PCT(100), LV_SIZE_CONTENT, BG, 0);
    lv_obj_set_flex_flow(confirm_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(confirm_row, 10, 0);
    button(confirm_row, "Looks right", width, GREEN, confirm_event, (void *)1);
    button(confirm_row, "Not right", width, PANEL, confirm_event, NULL);
    label(body, "HOW TO TEST", &lv_font_montserrat_12, m->accent);
    lv_obj_t *hint = label(body, m->instructions, &lv_font_montserrat_14, TEXT);
    lv_obj_set_width(hint, LV_PCT(100));
    lv_obj_t *api = box(body, LV_PCT(100), LV_SIZE_CONTENT, PANEL, 16);
    lv_obj_set_style_pad_all(api, 16, 0);
    lv_obj_set_style_pad_row(api, 10, 0);
    lv_obj_set_flex_flow(api, LV_FLEX_FLOW_COLUMN);
    label(api, "LEARN THE API", &lv_font_montserrat_12, m->accent);
    lv_obj_t *code = label(api, m->api, &lv_font_montserrat_12, TEXT);
    lv_obj_set_width(code, LV_PCT(100));
    shown_revision = UINT32_MAX;
    refresh(NULL);
}

/* 按结果版本刷新卡片和详情，并根据 busy 控制操作按钮可用性。 */
static void refresh(lv_timer_t *timer)
{
    (void)timer;
    unsigned passed = 0, tested = 0;
    if (gesture_tracking && !lab_touch_gesture_allowed()) gesture_blocked = true;
    bool busy = lab_busy();
    for (int i = 0; i < LAB_COUNT; ++i) {
        lab_result_t r;
        lab_snapshot((lab_id_t)i, &r);
        passed += r.status == LAB_PASS;
        tested += r.status == LAB_PASS || r.status == LAB_FAIL;
        if (card_revision[i] != r.revision) {
            lv_label_set_text(card_status[i], status_name(r.status));
            lv_obj_set_style_text_color(card_status[i], lv_color_hex(status_color(r.status)), 0);
            card_revision[i] = r.revision;
        }
        if (selected == i && detail && (shown_revision != r.revision || last_busy != busy)) {
            if (r.status == LAB_RUNNING && r.samples) lv_label_set_text_fmt(state_label, "LIVE / %u samples", (unsigned)r.samples);
            else lv_label_set_text(state_label, status_name(r.status));
            lv_obj_set_style_text_color(state_label, lv_color_hex(status_color(r.status)), 0);
            lv_label_set_text(value_label, r.value);
            lv_label_set_text(detail_label, r.detail);
            lv_label_set_text(busy_label, r.status == LAB_RUNNING ? "Test running. You can go back anytime." : "Another test is running. Please wait.");
            if (r.status == LAB_OBSERVE && !busy) lv_obj_remove_flag(confirm_row, LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(confirm_row, LV_OBJ_FLAG_HIDDEN);
            if (busy) lv_obj_remove_flag(busy_label, LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(busy_label, LV_OBJ_FLAG_HIDDEN);
            if (busy && r.status == LAB_RUNNING) {
                lv_obj_remove_flag(stop_button, LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_height(detail_body, lv_display_get_vertical_resolution(NULL) - 166);
            } else {
                lv_obj_add_flag(stop_button, LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_height(detail_body, lv_display_get_vertical_resolution(NULL) - 94);
            }
            for (int j = 0; j < LAB_ACTION_COUNT; ++j) if (actions[j]) {
                if (busy) lv_obj_add_state(actions[j], LV_STATE_DISABLED);
                else lv_obj_remove_state(actions[j], LV_STATE_DISABLED);
            }
            if (selected == LAB_SPEAKER) for (unsigned i = 0; i < 2; ++i) {
                if (busy) lv_obj_add_state(speaker_sliders[i], LV_STATE_DISABLED);
                else lv_obj_remove_state(speaker_sliders[i], LV_STATE_DISABLED);
            }
            shown_revision = r.revision;
        }
    }
    char text[72];
    rt_snprintf(text, sizeof(text), "%u / %u verified", passed, LAB_COUNT);
    lv_label_set_text(summary, text);
    lv_bar_set_value(progress, (int)(100 * tested / LAB_COUNT), LV_ANIM_ON);
    last_busy = busy;
}

/* 创建首页和外设卡片，注册导航输入与周期刷新定时器。 */
void lab_ui_create(void)
{
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(BG), 0);
    dashboard = box(lv_screen_active(), LV_PCT(100), LV_PCT(100), BG, 0);
    lv_obj_t *eyebrow = label(dashboard, "HUANGSHAN  /  HARDWARE", &lv_font_montserrat_12, GREEN);
    lv_obj_set_pos(eyebrow, 24, 20);
    lv_obj_t *title = label(dashboard, "HapticPod Lab", &lv_font_montserrat_28, TEXT);
    lv_obj_set_pos(title, 22, 43);
    summary = label(dashboard, "Ready to verify", &lv_font_montserrat_14, MUTED);
    lv_obj_set_pos(summary, 24, 84);
    progress = lv_bar_create(dashboard);
    lv_obj_set_pos(progress, 230, 90);
    lv_obj_set_size(progress, lv_display_get_horizontal_resolution(NULL)-254, 5);
    lv_obj_set_style_bg_color(progress, lv_color_hex(PANEL), LV_PART_MAIN);
    lv_obj_set_style_bg_color(progress, lv_color_hex(GREEN), LV_PART_INDICATOR);
    lv_obj_t *grid = box(dashboard, LV_PCT(100), lv_display_get_vertical_resolution(NULL)-126, BG, 0);
    lv_obj_set_pos(grid, 0, 116);
    lv_obj_add_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(grid, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(grid, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_pad_hor(grid, 22, 0);
    lv_obj_set_style_pad_bottom(grid, 22, 0);
    lv_obj_set_style_pad_gap(grid, 12, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    int width = (lv_display_get_horizontal_resolution(NULL)-56)/2;
    /* 将光照、惯性和磁场采样入口放在首页靠前的位置。 */
    static const lab_id_t order[] = {LAB_RGB, LAB_KEY, LAB_LIGHT, LAB_ADC, LAB_IMU, LAB_MAG,
                                   LAB_MIC, LAB_SPEAKER, LAB_GPIO, LAB_UART, LAB_CHARGER, LAB_RTC, LAB_DISPLAY, LAB_TOUCH, LAB_BLE, LAB_PAN, LAB_GAME};
    for (int slot = 0; slot < LAB_COUNT; ++slot) {
        int i = order[slot];
        const lab_module_t *m = &lab_modules[i];
        cards[i] = box(grid, width, 124, PANEL, 20);
        lv_obj_add_flag(cards[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(cards[i], lv_color_hex(0x253750), LV_STATE_PRESSED);
        lv_obj_add_event_cb(cards[i], open_detail, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        lv_obj_t *icon = label(cards[i], m->icon, &lv_font_montserrat_20, m->accent);
        lv_obj_set_pos(icon, 16, 14);
        lv_obj_t *name = label(cards[i], m->title, &lv_font_montserrat_14, TEXT);
        lv_obj_set_pos(name, 14, 48);
        lv_obj_set_width(name, width-24);
        lv_obj_t *sub = label(cards[i], m->subtitle, &lv_font_montserrat_12, MUTED);
        lv_obj_set_pos(sub, 14, 72);
        lv_obj_set_width(sub, width-24);
        lv_label_set_long_mode(sub, LV_LABEL_LONG_DOT);
        card_status[i] = label(cards[i], "NOT TESTED", &lv_font_montserrat_12, MUTED);
        lv_obj_set_pos(card_status[i], 14, 99);
        card_revision[i] = UINT32_MAX;
    }
    lv_obj_t *navigation_hint = label(dashboard, "Edge: left to back / bottom to home", &lv_font_montserrat_12, MUTED);
    lv_obj_align(navigation_hint, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_height(grid, lv_display_get_vertical_resolution(NULL) - 148);
    for (lv_indev_t *input = lv_indev_get_next(NULL); input; input = lv_indev_get_next(input)) {
        if (lv_indev_get_type(input) == LV_INDEV_TYPE_POINTER)
            lv_indev_add_event_cb(input, gesture_event, LV_EVENT_ALL, input);
    }
    lv_timer_create(refresh, 100, NULL);
    refresh(NULL);
}

static void close_overlay_event(lv_event_t *e) { (void)e; lab_overlay_close(); }

/* 在顶层创建独立测试页，保存其退出清理回调。 */
lv_obj_t *lab_overlay(const char *title, void (*close_cb)(void))
{
    if (overlay) lab_overlay_close();
    overlay_on_close = close_cb;
    overlay = box(lv_layer_top(), LV_PCT(100), LV_PCT(100), BG, 0);
    lv_obj_t *heading = label(overlay, title, &lv_font_montserrat_18, TEXT);
    lv_obj_align(heading, LV_ALIGN_TOP_MID, 0, 24);
    lv_obj_t *close = button(overlay, "Done / Back", 160, PANEL, close_overlay_event, NULL);
    lv_obj_align(close, LV_ALIGN_BOTTOM_MID, 0, -20);
    return overlay;
}
/* 先调用模块清理回调，再销毁全屏层，避免定时器继续访问旧控件。 */
void lab_overlay_close(void)
{
    if (!overlay) return;
    if (overlay_on_close) overlay_on_close();
    lv_obj_delete(overlay);
    overlay = NULL;
    overlay_on_close = NULL;
}
