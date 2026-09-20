/*
 * lab_touch_capture.h — 触摸采集接口：保存 SDK 已读取的原始帧、时间和序号，供双点诊断使用。
 */
#ifndef LAB_TOUCH_CAPTURE_H
#define LAB_TOUCH_CAPTURE_H
#include "lab.h"
typedef struct { uint32_t sequence; rt_tick_t tick; uint8_t data[13]; } lab_touch_capture_t;
/* 启停 SDK 帧观察并重置序号，不新增触摸数据读取。 */
void lab_touch_capture_enable(bool enabled);
/* 复制最近一帧；sequence 可用于判断是否收到新数据。 */
void lab_touch_capture_snapshot(lab_touch_capture_t *sample);
#endif
