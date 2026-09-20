/*
 * touch_capture.c — 触摸旁路采集：包装 SDK 的 I2C 传输，复制已读取的触摸帧，不改变原传输结果。
 */
#include "lab_touch_capture.h"
#include <string.h>
#include <rthw.h>

static struct rt_i2c_bus_device *watched_bus;
static lab_touch_capture_t latest;
extern rt_size_t __real_rt_i2c_transfer(struct rt_i2c_bus_device *, struct rt_i2c_msg *, rt_uint32_t);

/* 开启或关闭指定总线的帧观察，同时清空上一轮数据。 */
void lab_touch_capture_enable(bool enabled)
{
    struct rt_i2c_bus_device *bus = enabled ? rt_i2c_bus_device_find("i2c1") : NULL;
    rt_base_t level = rt_hw_interrupt_disable();
    watched_bus = bus;
    memset(&latest, 0, sizeof(latest));
    rt_hw_interrupt_enable(level);
}
/* 在短临界区复制整帧，保证数据、序号和采样时间一致。 */
void lab_touch_capture_snapshot(lab_touch_capture_t *sample)
{
    rt_base_t level = rt_hw_interrupt_disable();
    *sample = latest;
    rt_hw_interrupt_enable(level);
}
/* 链接器 --wrap 旁路观察 SDK 的 FT6146 读帧：不增添读操作，不修改原缓冲。 */
rt_size_t __wrap_rt_i2c_transfer(struct rt_i2c_bus_device *bus, struct rt_i2c_msg *msg, rt_uint32_t count)
{
    rt_size_t result = __real_rt_i2c_transfer(bus, msg, count);
    if (result == 2 && count == 2 && msg[0].addr == 0x38 && msg[0].len == 1 &&
        !(msg[0].flags & RT_I2C_RD) && msg[0].buf[0] == 0x01 &&
        (msg[1].flags & RT_I2C_RD) && msg[1].len >= 14) {
        rt_base_t level = rt_hw_interrupt_disable();
        if (bus == watched_bus) {
            memcpy(latest.data, msg[1].buf + 1, sizeof(latest.data));
            latest.tick = rt_tick_get();
            latest.sequence++;
        }
        rt_hw_interrupt_enable(level);
    }
    return result;
}
