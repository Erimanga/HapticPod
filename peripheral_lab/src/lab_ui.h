/*
 * lab_ui.h — 界面公共接口：提供全屏测试层及边缘手势判断，仅在 LVGL 线程中操作界面。
 */
#ifndef LAB_UI_H
#define LAB_UI_H
#include "lvgl.h"
/* 显示和触摸测试共用的全屏层；关闭不会销毁主界面。 */
lv_obj_t *lab_overlay(const char *title, void (*close_cb)(void));
/* 先通知测试模块清理，再销毁全屏层。 */
void lab_overlay_close(void);
/* 返回 0（不导航）、1（返回）或 2（首页），仅接受从边缘向内滑动。 */
int lab_ui_swipe_direction(int x, int y, int dx, int dy);
#endif
