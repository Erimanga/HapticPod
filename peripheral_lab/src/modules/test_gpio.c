/*
 * test_gpio.c — GPIO/马达测试：控制 PA20 输出短脉冲或低电平，实物响应由用户确认。
 */
#include "lab.h"
#include "bf0_hal.h"
#include "drv_io.h"

/* 设置 GPIO 输出模式，按动作产生脉冲，并在结束时回到低电平。 */
void lab_gpio_run(unsigned action)
{
    /* 先把引脚复用选为 GPIO，再用 RT-Thread 配置输入/输出方向。 */
    HAL_PIN_Set(PAD_PA20, GPIO_A20, PIN_NOPULL, 1);
    rt_pin_mode(20, PIN_MODE_OUTPUT);
    if (action == 0) {
        /* 写高电平启动输出，延时在后台任务内执行。 */
        rt_pin_write(20, PIN_HIGH);
        rt_thread_mdelay(200);
    }
    /* 脉冲结束主动拉低；电平回读不能代替马达实际转动的观察。 */
    rt_pin_write(20, PIN_LOW);
    if (rt_pin_read(20) != PIN_LOW) lab_error(LAB_GPIO, "LOW readback", -RT_ERROR);
    else lab_publish(LAB_GPIO, LAB_OBSERVE, action == 0 ? "Pulse complete" : "Output LOW",
                     "PA20 is now LOW. Confirm the physical response; readback is not a load test.");
}
