/*
 * lab.h — 外设测试公共接口：定义模块、动作和结果，连接界面、后台任务与设备访问层。
 */
#ifndef PERIPHERAL_LAB_H
#define PERIPHERAL_LAB_H

#include <rtthread.h>
#include <rtdevice.h>
#include <stdint.h>
#include <stdbool.h>

/* 模块只发布数据；所有 LVGL 对象由 UI 线程管理。 */
#define LAB_ACTION_COUNT 10

/* OBSERVE 表示命令已完成但需观察实物；PASS 的含义由各测试的判定条件决定。 */
typedef enum { LAB_IDLE, LAB_RUNNING, LAB_PASS, LAB_FAIL, LAB_OBSERVE } lab_status_t;
typedef enum {
    LAB_RGB, LAB_GPIO, LAB_KEY, LAB_ADC, LAB_UART, LAB_CHARGER,
    LAB_IMU, LAB_LIGHT, LAB_MAG, LAB_RTC, LAB_DISPLAY, LAB_TOUCH, LAB_MIC, LAB_SPEAKER, LAB_BLE, LAB_PAN, LAB_GAME, LAB_COUNT
} lab_id_t;
/* 结果以整份快照跨线程传递，revision 用于让 UI 只刷新已变化的内容。 */
typedef struct {
    lab_status_t status;
    uint32_t revision;
    uint32_t samples;
    char value[80];
    char detail[192];
} lab_result_t;
/* 注册表同时描述界面和动作；run 为空的测试由界面层专门处理。 */
typedef struct {
    const char *title, *subtitle, *icon;
    uint32_t accent;
    const char *instructions;
    const char *api;
    const char *actions[LAB_ACTION_COUNT];
    void (*run)(unsigned action);
} lab_module_t;

extern const lab_module_t lab_modules[LAB_COUNT];
/* 初始化测试调度；普通外设动作经 lab_start 提交，不从 UI 直接执行 run。 */
int lab_init(void);
bool lab_start(lab_id_t id, unsigned action);
/* 查询普通外设 worker 的占用状态，不代表 BLE/PAN 专用任务的活动状态。 */
bool lab_busy(void);
/* 扬声器测试设置在页面切换后保留；只在空闲时接受新设置。 */
void lab_speaker_settings(unsigned *volume, unsigned *frequency);
void lab_speaker_configure(unsigned volume, unsigned frequency);
extern const unsigned lab_speaker_notes[8];
/* 连续采样与停止接口：停止是请求，设备清理由工作线程完成。 */
bool lab_continuous(void);
void lab_stop(void);
bool lab_cancelled(void);
/* 原始帧保留两个触点；SDK 的 LVGL 输入仍用于普通单指操作。 */
typedef struct { uint16_t x, y; uint8_t id; } lab_touch_point_t;
typedef struct { unsigned count; lab_touch_point_t points[2]; } lab_touch_frame_t;
void lab_touch_frame_publish(const lab_touch_frame_t *frame);
void lab_touch_frame_snapshot(lab_touch_frame_t *frame);
bool lab_touch_decode(const uint8_t data[13], lab_touch_frame_t *frame);
void lab_touch_run(unsigned action);
/* 发布/读取测试结果及人工确认，内部使用状态锁保护共享数据。 */
void lab_publish(lab_id_t id, lab_status_t state, const char *value, const char *detail);
void lab_snapshot(lab_id_t id, lab_result_t *result);
void lab_confirm(lab_id_t id, bool pass);
void lab_ui_create(void);

/* 公共 I2C 操作返回错误，不用断言终止整套测试。 */
/* 查找板级总线，并完成本项目需要的 I2C3 引脚复用。 */
struct rt_i2c_bus_device *lab_i2c(const char *name);
/* 连续读寄存器；完整传输才成功，错误交由模块发布。 */
rt_err_t lab_reg_read(struct rt_i2c_bus_device *bus, uint8_t addr, uint8_t reg,
                     void *data, uint16_t size);
/* 写入一个寄存器字节，地址与数据在同一条消息中发送。 */
rt_err_t lab_reg_write(struct rt_i2c_bus_device *bus, uint8_t addr, uint8_t reg, uint8_t value);
/* 限时等待目标状态位全部置位，轮询间隙让出 CPU。 */
bool lab_poll_reg(struct rt_i2c_bus_device *bus, uint8_t addr, uint8_t reg,
                  uint8_t mask, unsigned timeout_ms);
void lab_error(lab_id_t id, const char *operation, int error);

/* 普通外设动作入口均由 lab_io 调用；action 对应注册表中的按钮下标。 */
#define LAB_RUN(name) void lab_##name##_run(unsigned action)
LAB_RUN(rgb); LAB_RUN(gpio); LAB_RUN(key); LAB_RUN(adc); LAB_RUN(uart);
LAB_RUN(mic); LAB_RUN(speaker);
LAB_RUN(charger); LAB_RUN(imu); LAB_RUN(light); LAB_RUN(mag); LAB_RUN(rtc);
/* 显示/触摸界面入口由 LVGL 线程调用，避免后台任务直接操作控件。 */
void lab_display_open(void);
void lab_touch_open(void);
void lab_touch_multi_open(void);
bool lab_touch_gesture_allowed(void);
#endif
