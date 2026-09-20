/*
 * test_charger.c — 充电芯片测试：经 I2C2 读取 AW32001 标识和状态，不修改充电参数。
 */
#include "lab.h"

/* 读取标识与状态寄存器；通过仅表示芯片可访问。 */
void lab_charger_run(unsigned action)
{
    (void)action;
    struct rt_i2c_bus_device *bus = lab_i2c("i2c2");
    uint8_t id, status;
    rt_err_t err = lab_reg_read(bus, 0x49, 0x0A, &id, 1);
    if (err == RT_EOK) err = lab_reg_read(bus, 0x49, 0x08, &status, 1);
    if (err != RT_EOK) { lab_error(LAB_CHARGER, "Read AW32001", err); return; }
    char value[64], detail[160];
    rt_snprintf(value, sizeof(value), "ID 0x%02X", id);
    rt_snprintf(detail, sizeof(detail), "I2C2 address 0x49 replied. Status[08]=0x%02X. Read-only bus check; no charging parameters changed.", status);
    lab_publish(LAB_CHARGER, LAB_PASS, value, detail);
}
