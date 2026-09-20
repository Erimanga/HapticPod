/*
 * main.c — 程序入口：初始化外设测试与蓝牙服务，在主线程中驱动 LVGL 界面。
 */
#include "lab.h"
#include "lab_ble.h"
#include "lab_pan.h"
#include "lvgl.h"
#include "littlevgl2rtt.h"

/* 先建立测试和显示环境，再进入 LVGL 定时器处理循环。 */
int main(void)
{
    /* lab_init 创建后台工作任务；littlevgl2rtt_init 将 LVGL 接到 SDK 的 lcd 驱动。 */
    if (lab_init() != RT_EOK || littlevgl2rtt_init("lcd") != RT_EOK) {
        rt_kprintf("Peripheral Lab: initialization failed\n");
        return -1;
    }
    /* 先注册经典蓝牙事件接收，再由 BLE 任务统一启用共享协议栈。 */
    if (lab_pan_init() != RT_EOK) rt_kprintf("PAN initialization failed\n");
    if (lab_ble_init() != RT_EOK) rt_kprintf("Peripheral Lab: BLE initialization failed\n");
    lab_ui_create();
    while (1) {
        /* 处理界面定时器、输入与重绘，返回建议的下次调用间隔。 */
        uint32_t delay = lv_timer_handler();
        /* 有持续动画时也让出 CPU；空闲时不超过 20 ms，保持触摸响应。 */
        if (delay < 2) delay = 2;
        if (delay > 20) delay = 20;
        rt_thread_mdelay(delay);
    }
}
