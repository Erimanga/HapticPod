/*
 * test_key.c — 按键测试：轮询 KEY1/KEY2，消抖后检查释放、按下、再释放的完整过程。
 */
#include "lab.h"
#include "bf0_hal.h"
#include "drv_io.h"

/* 在八秒窗口内检查完整按键过程，支持用户提前停止。 */
void lab_key_run(unsigned action)
{
    unsigned pin = action == 0 ? 43 : 34;
    if (pin == 43) {
        HAL_PIN_Set(PAD_PA43, GPIO_A43, PIN_NOPULL, 1);
        rt_pin_mode(43, PIN_MODE_INPUT);
    }
    /* KEY1 是电源/下载复用键，沿用 BSP 输入和上下拉配置，只读电平。 */
    bool baseline = false, pressed = false;
    int previous = -1, stable = 0;
    lab_publish(LAB_KEY, LAB_RUNNING, action == 0 ? "Press KEY2" : "Tap KEY1 briefly", "Release first, then briefly press and release within 8 seconds.");
    for (int i = 0; i < 400; ++i) {
        if (lab_cancelled()) {
            lab_publish(LAB_KEY, LAB_IDLE, "Button test stopped", "No complete key cycle recorded.");
            return;
        }
        /* 读取输入电平；连续三次一致才采用，过滤机械按键抖动。 */
        int level = rt_pin_read(pin);
        stable = level == previous ? stable + 1 : 1;
        previous = level;
        if (stable == 3) {
            if (!level && !pressed) baseline = true;
            if (level && baseline && !pressed) {
                pressed = true;
                lab_publish(LAB_KEY, LAB_RUNNING, "Now release", pin == 43 ? "KEY2 press detected." : "KEY1 press detected. Release now.");
            } else if (!level && pressed) {
                lab_publish(LAB_KEY, LAB_PASS, "Press + release", pin == 43 ? "KEY2 press and release verified." : "KEY1 press and release verified.");
                return;
            }
        }
        rt_thread_mdelay(20);
    }
    lab_publish(LAB_KEY, LAB_FAIL, "No full key cycle", "Timed out. Release the selected key, retry, then briefly press and release.");
}
