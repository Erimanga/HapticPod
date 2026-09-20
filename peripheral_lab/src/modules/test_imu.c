/*
 * test_imu.c — 六轴传感器测试：核对标识，临时启用采样，读取加速度和角速度后恢复配置。
 */
#include "lab.h"
#include "lab_units.h"

static int16_t axis(const uint8_t *p) { return (int16_t)((uint16_t)p[0] | (uint16_t)p[1] << 8); }

/* 完成一次六轴采样；保存的寄存器配置在读取成功或失败后均尝试恢复。 */
void lab_imu_run(unsigned action)
{
    (void)action;
    struct rt_i2c_bus_device *bus = lab_i2c("i2c3");
    uint8_t id = 0, old[3], data[12];
    uint8_t addr = 0x6A;
    /* 先读取 WHO_AM_I，避免在器件不匹配时写入采样配置。 */
    rt_err_t err = lab_reg_read(bus, addr, 0x0F, &id, 1);
    if (err != RT_EOK || id != 0x6A) { lab_error(LAB_IMU, "LSM6DSL identity", err ? err : -RT_ERROR); return; }
    err = lab_reg_read(bus, addr, 0x10, &old[0], 1);
    if (err == RT_EOK) err = lab_reg_read(bus, addr, 0x11, &old[1], 1);
    if (err == RT_EOK) err = lab_reg_read(bus, addr, 0x12, &old[2], 1);
    if (err != RT_EOK) { lab_error(LAB_IMU, "Save IMU settings", err); return; }
    /* BDU + 地址自增，104 Hz，量程 2 g / 245 dps。 */
    err = lab_reg_write(bus, addr, 0x12, (old[2] & ~0x02) | 0x44);
    if (err == RT_EOK) err = lab_reg_write(bus, addr, 0x10, 0x40);
    if (err == RT_EOK) err = lab_reg_write(bus, addr, 0x11, 0x40);
    /* 等待加速度与角速度都就绪，再连续读取两组各三轴数据。 */
    if (err == RT_EOK && !lab_poll_reg(bus, addr, 0x1E, 0x03, 500)) err = -RT_ETIMEOUT;
    if (err == RT_EOK) err = lab_reg_read(bus, addr, 0x22, data, sizeof(data));
    /* 无论采样是否成功，都尝试恢复已保存的三个控制寄存器。 */
    for (int i = 0; i < 3; ++i) {
        rt_err_t restore = lab_reg_write(bus, addr, 0x10 + i, old[i]);
        if (err == RT_EOK) err = restore;
    }
    if (err != RT_EOK) { lab_error(LAB_IMU, "Read / restore IMU", err); return; }
    int16_t a[3], g[3];
    char av[3][16], gv[3][16], value[80], detail[192];
    for (int i = 0; i < 3; ++i) {
        a[i] = axis(data + 6 + i * 2); g[i] = axis(data + i * 2);
        lab_fixed3(av[i], sizeof(av[i]), lab_accel_millig(a[i]));
        lab_fixed3(gv[i], sizeof(gv[i]), lab_gyro_millidps(g[i]));
    }
    rt_snprintf(value, sizeof(value), "Accel (g)\nX %s\nY %s   Z %s", av[0], av[1], av[2]);
    rt_snprintf(detail, sizeof(detail), "Gyro (deg/s)\nX %s / Y %s / Z %s\nRaw A: %d %d %d\nRaw G: %d %d %d\nNominal scale; not calibrated.", gv[0], gv[1], gv[2], a[0], a[1], a[2], g[0], g[1], g[2]);
    lab_publish(LAB_IMU, LAB_PASS, value, detail);
}
