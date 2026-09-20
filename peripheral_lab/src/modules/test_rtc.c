/*
 * test_rtc.c — RTC 测试：间隔读取两次时间，验证时钟走动，不设置或校准日历。
 */
#include "lab.h"
#include <time.h>

/* 只读 RTC 时间，以两次读数的差值检查计时是否前进。 */
void lab_rtc_run(unsigned action)
{
    (void)action;
    rt_device_t rtc = rt_device_find("rtc");
    time_t first = 0, second = 0;
    if (!rtc) { lab_error(LAB_RTC, "Find rtc", -RT_ENOSYS); return; }
    /* RTC 控制命令返回当前时间，不修改时钟设置。 */
    rt_err_t err = rt_device_control(rtc, RT_DEVICE_CTRL_RTC_GET_TIME, &first);
    if (err != RT_EOK) { lab_error(LAB_RTC, "Read RTC", err); return; }
    rt_thread_mdelay(1200);
    err = rt_device_control(rtc, RT_DEVICE_CTRL_RTC_GET_TIME, &second);
    if (err != RT_EOK) { lab_error(LAB_RTC, "Read RTC", err); return; }
    long delta = (long)(second - first);
    char value[64], detail[160];
    rt_snprintf(value, sizeof(value), "+%ld second%s", delta, delta == 1 ? "" : "s");
    rt_snprintf(detail, sizeof(detail), "RTC epoch: %lu. Measured across 1.2 s. Calendar was not changed; this checks ticking only.", (unsigned long)second);
    lab_publish(LAB_RTC, delta >= 1 && delta <= 2 ? LAB_PASS : LAB_FAIL, value, detail);
}
