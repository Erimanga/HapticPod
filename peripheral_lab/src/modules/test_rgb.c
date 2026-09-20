/*
 * test_rgb.c — RGB 灯测试：通过 PWM/DMA 驱动发送颜色，支持三色轮播、单色保持和熄灭。
 */
#include "lab.h"
#include "bf0_hal.h"
#include "drv_io.h"
#include "drivers/rt_drv_pwm.h"

/* 通过设备控制命令提交 RGB 颜色，由驱动生成灯珠所需时序。 */
static rt_err_t set_color(rt_device_t dev, uint32_t color)
{
    /* dma_type = 0：UPDATE DMA，与本板 PWM RGB 驱动配置一致。 */
    struct rt_rgbled_configuration cfg = {0};
    cfg.color_rgb = color;
    return rt_device_control(dev, PWM_CMD_SET_COLOR, &cfg);
}

/* 使能供电并配置 PWM 引脚，按动作轮播、保持颜色或熄灯。 */
void lab_rgb_run(unsigned action)
{
    static const uint32_t colors[] = {0x300000, 0x003000, 0x000030};
    static const char *names[] = {"RED", "GREEN", "BLUE"};
    rt_device_t dev = rt_device_find("rgbled");
    rt_err_t err = -RT_ENOSYS;
    if (!dev) { lab_error(LAB_RGB, "Find rgbled", err); return; }
    /* 使能灯的外设电源，并将 PA32 连接到定时器 PWM 输出。 */
    HAL_PMU_ConfigPeriLdo(PMU_PERI_LDO3_3V3, true, true);
    HAL_PIN_Set(PAD_PA32, GPTIM2_CH1, PIN_NOPULL, 1);
    if (action == 0) {
        for (unsigned i = 0; i < 3; ++i) {
            lab_publish(LAB_RGB, LAB_RUNNING, names[i], "Look at the board LED. Each color lasts one second.");
            err = set_color(dev, colors[i]);
            if (err != RT_EOK) break;
            rt_thread_mdelay(1000);
        }
        rt_err_t off = set_color(dev, 0);
        if (err == RT_EOK) err = off;
    } else {
        err = set_color(dev, action == 1 ? colors[0] : action == 2 ? colors[2] : 0);
    }
    if (err != RT_EOK) lab_error(LAB_RGB, "PWM color write", err);
    else lab_publish(LAB_RGB, LAB_OBSERVE, action == 0 ? "Cycle complete" : action == 3 ? "LED off" : "Color sent",
                     "Did the LED respond correctly? Confirm below. Use Off when finished.");
}
