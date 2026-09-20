/*
 * test_light.c — 环境光测试：读取 LTR303 双通道数据，按实际增益和积分时间估算照度。
 */
#include "lab.h"
#include "lab_units.h"

/* 暂时启用 ALS，等待新数据、读取双通道，恢复控制寄存器后换算照度。 */
void lab_light_run(unsigned action)
{
    (void)action;
    struct rt_i2c_bus_device *bus = lab_i2c("i2c3");
    uint8_t part, maker, old, rate = 0, data[4];
    rt_err_t err = lab_reg_read(bus, 0x29, 0x86, &part, 1);
    if (err == RT_EOK) err = lab_reg_read(bus, 0x29, 0x87, &maker, 1);
    if (err != RT_EOK || (part & 0xF0) != 0xA0 || maker != 0x05) {
        lab_error(LAB_LIGHT, "LTR303 identity", err ? err : -RT_ERROR); return;
    }
    err = lab_reg_read(bus, 0x29, 0x80, &old, 1);
    if (err != RT_EOK) { lab_error(LAB_LIGHT, "Save ALS settings", err); return; }
    err = lab_reg_read(bus, 0x29, 0x85, &rate, 1);
    /* 保留其他控制位，只置位 ALS 使能位，随后等待数据就绪。 */
    if (err == RT_EOK) err = lab_reg_write(bus, 0x29, 0x80, old | 1);
    if (err == RT_EOK && !lab_poll_reg(bus, 0x29, 0x8C, 0x04, 2500)) err = -RT_ETIMEOUT;
    uint8_t status = 0;
    if (err == RT_EOK) err = lab_reg_read(bus, 0x29, 0x8C, &status, 1);
    if (err == RT_EOK && (status & 0x80)) err = -RT_ERROR;
    /* 按数据手册一次读取 CH1、CH0，避免跨次测量拼接。 */
    if (err == RT_EOK) err = lab_reg_read(bus, 0x29, 0x88, data, sizeof(data));
    rt_err_t restore = lab_reg_write(bus, 0x29, 0x80, old);
    if (err == RT_EOK) err = restore;
    if (err != RT_EOK) { lab_error(LAB_LIGHT, "Read / restore ALS", err); return; }
    uint16_t ch0 = data[2] | (uint16_t)data[3] << 8;
    uint16_t ch1 = data[0] | (uint16_t)data[1] << 8;
    int32_t lux;
    char value[80], detail[192], number[24];
    bool valid = lab_lux_milli(ch0, ch1, status >> 4, rate, &lux);
    if (valid) {
        lab_fixed3(number, sizeof(number), lux);
        rt_snprintf(value, sizeof(value), "%s lux", number);
    } else rt_snprintf(value, sizeof(value), "Lux unavailable");
    rt_snprintf(detail, sizeof(detail), "Raw CH0 %u / CH1 %u\nGain code %u, rate 0x%02X\n%s", ch0, ch1, (status >> 4) & 7, rate,
                valid ? "Estimated illuminance; no cover-glass calibration." : "Saturated, reserved gain or IR-dominant reading. Raw data remains available.");
    lab_publish(LAB_LIGHT, LAB_PASS, value, detail);
}
