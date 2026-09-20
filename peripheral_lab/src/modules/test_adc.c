/*
 * test_adc.c — 电池 ADC 测试：读取 bat1 通道 7 的 SDK 校准电压，采样后关闭通道。
 */
#include "lab.h"

/* 完成一次启用、读数、关闭流程；连续采样由上层 worker 重复调用。 */
void lab_adc_run(unsigned action)
{
    (void)action;
    /* 按 BSP 注册名查找设备；找到句柄不代表通道已启用。 */
    rt_adc_device_t adc = (rt_adc_device_t)rt_device_find("bat1");
    rt_adc_cmd_read_arg_t arg = {.channel = 7};
    if (!adc) { lab_error(LAB_ADC, "Find bat1", -RT_ENOSYS); return; }
    /* 打开采样通道，并检查驱动返回值。 */
    rt_err_t err = rt_adc_enable(adc, arg.channel);
    if (err != RT_EOK) { lab_error(LAB_ADC, "Enable ADC", err); return; }
    /* 通过 SDK ADC 控制命令读取电压，结果写入 arg。 */
    err = rt_device_control((rt_device_t)adc, RT_ADC_CMD_READ, &arg);
    /* 读取失败也关闭通道，避免测试结束后继续占用采样资源。 */
    rt_err_t stop = rt_adc_disable(adc, arg.channel);
    if (err == RT_EOK) err = stop;
    if (err != RT_EOK) { lab_error(LAB_ADC, "Read ADC", err); return; }
    char value[64];
    /* SDK 的 value 单位为 0.1 mV，不把读数包装成电量百分比。 */
    rt_snprintf(value, sizeof(value), "%u.%04u V", (unsigned)(arg.value / 10000), (unsigned)(arg.value % 10000));
    char detail[160];
    rt_snprintf(detail, sizeof(detail), "SDK value: %lu (0.1 mV units). This API already calibrates ADC codes. Compare volts with a meter; not a battery health estimate.", (unsigned long)arg.value);
    lab_publish(LAB_ADC, LAB_PASS, value, detail);
}
