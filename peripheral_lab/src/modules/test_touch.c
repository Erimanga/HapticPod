/*
 * test_touch.c — 触摸测试界面：单点检查目标覆盖，双点显示后台解码的真实触点与诊断信息。
 */
#include "lab.h"
#include "lab_ui.h"

static unsigned hits;
static lv_obj_t *feedback, *targets[4];
static bool multi_active;
static lv_timer_t *multi_timer;
static lv_obj_t *markers[2], *marker_labels[2], *multi_feedback, *multi_diagnostic;

/* 记录命中的目标和坐标；位图避免重复点击被多次计数。 */
static void target_hit(lv_event_t *e)
{
    unsigned index = (unsigned)(uintptr_t)lv_event_get_user_data(e);
    lv_point_t point = {0, 0};
    lv_indev_t *input = lv_indev_active();
    if (input) lv_indev_get_point(input, &point);
    hits |= 1u << index;
    unsigned count = 0;
    for (unsigned i = 0; i < 4; ++i) count += !!(hits & (1u << i));
    lv_obj_set_style_bg_color(lv_event_get_target_obj(e), lv_color_hex(0x86F5CA), 0);
    for (unsigned i = 0; i < 4; ++i) {
        lv_obj_set_style_border_width(targets[i], i == index ? 3 : 0, 0);
        lv_obj_set_style_border_color(targets[i], lv_color_hex(0xFFFFFF), 0);
    }
    char text[80];
    rt_snprintf(text, sizeof(text), "%u / 4 targets\nTarget %u HIT\nx %d   y %d", count, index + 1, (int)point.x, (int)point.y);
    lv_label_set_text(feedback, text);
    lab_publish(LAB_TOUCH, hits == 15 ? LAB_PASS : LAB_RUNNING,
                hits == 15 ? "All targets reached" : "Keep tapping", text);
}
static void closed(void)
{
    if (hits != 15) lab_publish(LAB_TOUCH, LAB_IDLE, "Touch test incomplete", "Reach all four targets to pass. Start again when ready.");
}
/* 创建四个目标，验证基本单点触摸覆盖。 */
void lab_touch_open(void)
{
    hits = 0;
    lv_obj_t *root = lab_overlay("TOUCH / FOUR TARGETS", closed);
    feedback = lv_label_create(root);
    lv_obj_set_style_text_font(feedback, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(feedback, lv_color_hex(0xEDF4FC), 0);
    lv_obj_set_style_text_align(feedback, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(feedback, "0 / 4 targets\nTap each numbered circle");
    lv_obj_align(feedback, LV_ALIGN_CENTER, 0, -12);
    const lv_align_t positions[] = {LV_ALIGN_TOP_LEFT, LV_ALIGN_TOP_RIGHT, LV_ALIGN_BOTTOM_LEFT, LV_ALIGN_BOTTOM_RIGHT};
    for (unsigned i = 0; i < 4; ++i) {
        lv_obj_t *target = targets[i] = lv_button_create(root);
        lv_obj_set_size(target, 72, 72);
        lv_obj_set_style_radius(target, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(target, lv_color_hex(0x80CCFF), 0);
        lv_obj_set_style_shadow_width(target, 0, 0);
        lv_obj_align(target, positions[i], (i & 1) ? -24 : 24, i < 2 ? 72 : -94);
        lv_obj_add_event_cb(target, target_hit, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        lv_obj_t *number = lv_label_create(target);
        lv_label_set_text_fmt(number, "%u", i + 1);
        lv_obj_set_style_text_font(number, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(number, lv_color_hex(0x0C1220), 0);
        lv_obj_center(number);
    }
    lab_publish(LAB_TOUCH, LAB_RUNNING, "Waiting for touch", "Tap each numbered circle once. Done / Back exits at any time.");
}


/* 双指诊断中有两个触点时屏蔽导航，避免测试手势触发离页。 */
bool lab_touch_gesture_allowed(void)
{
    if (!multi_active) return true;
    lab_touch_frame_t frame;
    lab_touch_frame_snapshot(&frame);
    return frame.count < 2;
}
/* 定时读取后台帧并更新两个标记，不在界面线程执行 I2C。 */
static void multi_refresh(lv_timer_t *timer)
{
    (void)timer;
    lab_touch_frame_t frame;
    lab_result_t result;
    lab_touch_frame_snapshot(&frame);
    lab_snapshot(LAB_TOUCH, &result);
    if (result.status == LAB_FAIL) lv_label_set_text(multi_feedback, result.detail);
    else lv_label_set_text_fmt(multi_feedback, "%u / 2 fingers  |  %s", frame.count,
                              result.status == LAB_PASS ? "VERIFIED" : "Hold both for 250 ms");
    lv_label_set_text(multi_diagnostic, result.detail);
    for (unsigned i = 0; i < 2; ++i) {
        if (i >= frame.count) { lv_obj_add_flag(markers[i], LV_OBJ_FLAG_HIDDEN); continue; }
        lv_obj_remove_flag(markers[i], LV_OBJ_FLAG_HIDDEN);
        const lab_touch_point_t *p = &frame.points[i];
        lv_obj_set_pos(markers[i], LV_CLAMP(0, (int)p->x - 35, 320), LV_CLAMP(0, (int)p->y - 35, 380));
        lv_label_set_text_fmt(marker_labels[i], "ID %u\n%u,%u", (unsigned)p->id, (unsigned)p->x, (unsigned)p->y);
        lv_obj_center(marker_labels[i]);
    }
}
/* 先删除刷新定时器，再请求后台停止触摸诊断。 */
static void multi_closed(void)
{
    if (multi_timer) { lv_timer_delete(multi_timer); multi_timer = NULL; }
    multi_active = false;
    lab_stop();
}
/* 启动原始帧诊断，并建立与真实触点一一对应的显示标记。 */
void lab_touch_multi_open(void)
{
    if (lab_busy()) return;
    lab_touch_frame_t empty = {0};
    lab_touch_frame_publish(&empty);
    if (!lab_start(LAB_TOUCH, 1)) return;
    lv_obj_t *root = lab_overlay("TOUCH / TWO FINGERS", multi_closed);
    multi_active = true;
    multi_feedback = lv_label_create(root);
    lv_obj_set_width(multi_feedback, 342);
    lv_obj_set_style_text_font(multi_feedback, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(multi_feedback, lv_color_hex(0xEDF4FC), 0);
    lv_obj_set_style_text_align(multi_feedback, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(multi_feedback, LV_ALIGN_TOP_MID, 0, 68);
    multi_diagnostic = lv_label_create(root);
    lv_obj_set_width(multi_diagnostic, 342);
    lv_obj_set_style_text_font(multi_diagnostic, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(multi_diagnostic, lv_color_hex(0x97A8C0), 0);
    lv_obj_set_style_text_align(multi_diagnostic, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(multi_diagnostic, LV_ALIGN_TOP_MID, 0, 98);
    for (unsigned i = 0; i < 2; ++i) {
        markers[i] = lv_obj_create(root);
        lv_obj_remove_style_all(markers[i]);
        lv_obj_remove_flag(markers[i], LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(markers[i], 70, 70);
        lv_obj_set_style_radius(markers[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(markers[i], 3, 0);
        lv_obj_set_style_border_color(markers[i], lv_color_hex(i ? 0xFFC580 : 0x86F5CA), 0);
        marker_labels[i] = lv_label_create(markers[i]);
        lv_obj_set_style_text_font(marker_labels[i], &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(marker_labels[i], lv_color_hex(0xEDF4FC), 0);
        lv_obj_set_style_text_align(marker_labels[i], LV_TEXT_ALIGN_CENTER, 0);
    }
    /* 返回按钮始终位于触点标记之上。 */
    lv_obj_move_foreground(lv_obj_get_child(root, 1));
    multi_timer = lv_timer_create(multi_refresh, 50, NULL);
    multi_refresh(NULL);
}
