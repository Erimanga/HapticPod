/*
 * test_display.c — 屏幕测试：用 LVGL 切换五种纯色，全部浏览后由用户确认显示效果。
 */
#include "lab.h"
#include "lab_ui.h"

static const uint32_t colors[] = {0xFF0000, 0x00FF00, 0x0000FF, 0xFFFFFF, 0x000000};
static const char *names[] = {"RED", "GREEN", "BLUE", "WHITE", "BLACK"};
static lv_obj_t *surface, *caption, *background;
static unsigned pattern, seen;

/* 设置当前纯色并记录浏览位，五个位全置位表示全部颜色已查看。 */
static void show_pattern(void)
{
    char text[64];
    seen |= 1u << pattern;
    lv_obj_set_style_bg_color(surface, lv_color_hex(colors[pattern]), 0);
    lv_obj_set_style_bg_color(background, lv_color_hex(colors[pattern]), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(background, 0), lv_color_hex(pattern == 1 || pattern == 3 ? 0x101010 : 0xFFFFFF), 0);
    lv_obj_set_style_text_color(caption, lv_color_hex(pattern == 1 || pattern == 3 ? 0x101010 : 0xFFFFFF), 0);
    rt_snprintf(text, sizeof(text), "%s  /  %u of 5\nTap here for next color", names[pattern], pattern + 1);
    lv_label_set_text(caption, text);
}
static void next_pattern(lv_event_t *e)
{
    (void)e;
    pattern = (pattern + 1) % 5;
    show_pattern();
}
/* 离页时报告浏览是否完整；画面质量仍需用户观察确认。 */
static void closed(void)
{
    lab_publish(LAB_DISPLAY, seen == 31 ? LAB_OBSERVE : LAB_IDLE,
                seen == 31 ? "Patterns complete" : "Patterns incomplete",
                seen == 31 ? "Confirm that all colors and pixels looked correct." : "Open again and view all five colors before confirming.");
}
/* 创建颜色测试层，点击画面循环切换颜色。 */
void lab_display_open(void)
{
    pattern = seen = 0;
    lv_obj_t *root = lab_overlay("DISPLAY / COLOR CHECK", closed);
    background = root;
    surface = lv_obj_create(root);
    lv_obj_remove_style_all(surface);
    lv_obj_set_size(surface, LV_PCT(100), lv_display_get_vertical_resolution(NULL) - 152);
    lv_obj_set_pos(surface, 0, 64);
    lv_obj_set_style_bg_opa(surface, LV_OPA_COVER, 0);
    lv_obj_remove_flag(surface, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(surface, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(surface, next_pattern, LV_EVENT_CLICKED, NULL);
    caption = lv_label_create(surface);
    lv_obj_set_style_text_font(caption, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(caption);
    show_pattern();
    lab_publish(LAB_DISPLAY, LAB_RUNNING, "Viewing patterns", "Tap the color area to cycle through five colors.");
}
