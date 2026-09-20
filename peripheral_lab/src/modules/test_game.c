/*
 * test_game.c — 游戏传感器任务：在 lab_io 中持续采集 IMU 并发布快照，退出时恢复传感器配置。
 */
/* Runs exclusively in lab_io; never performs I2C in the LVGL thread. */
#include "lab.h"
#include "lab_game.h"
#include "lab_game_sound.h"
static int16_t word(const uint8_t *p) { return (int16_t)(p[0] | (uint16_t)p[1] << 8); }
/* 持续采样并发布运动快照；离页后等待音效关闭，再恢复 IMU 原配置。 */
void lab_game_run(unsigned action)
{
    (void)action;
    struct rt_i2c_bus_device *bus = lab_i2c("i2c3");
    uint8_t id = 0, old[3], data[12];
    lab_motion_t sample = {0};
    rt_err_t err = lab_reg_read(bus, 0x6A, 0x0F, &id, 1);
    if (err != RT_EOK || id != 0x6A) { lab_error(LAB_GAME, "LSM6DSL identity", err ? err : -RT_ERROR); return; }
    err = lab_reg_read(bus, 0x6A, 0x10, old, 1);
    if (!err) err = lab_reg_read(bus, 0x6A, 0x11, old + 1, 1);
    if (!err) err = lab_reg_read(bus, 0x6A, 0x12, old + 2, 1);
    if (err) { lab_error(LAB_GAME, "Save IMU", err); return; }
    err = lab_reg_write(bus, 0x6A, 0x12, (old[2] & ~0x02) | 0x44);
    if (!err) err = lab_reg_write(bus, 0x6A, 0x10, 0x40);
    if (!err) err = lab_reg_write(bus, 0x6A, 0x11, 0x40);
    if (!err) lab_game_sound_begin();
    unsigned missed = 0;
    while (!err && !lab_cancelled()) {
        uint8_t ready = 0;
        err = lab_reg_read(bus, 0x6A, 0x1E, &ready, 1);
        if (!err && (ready & 3) == 3) {
            err = lab_reg_read(bus, 0x6A, 0x22, data, sizeof(data));
            if (!err) {
                for (unsigned i = 0; i < 3; ++i) {
                    sample.g[i] = word(data + i * 2) * 0.00875f;
                    sample.a[i] = word(data + 6 + i * 2) * 0.000061f;
                }
                sample.valid = true; sample.sequence++;
                lab_motion_publish(&sample);
                missed = 0;
            }
        } else if (!err && ++missed > 25) err = -RT_ETIMEOUT;
        rt_thread_mdelay(20);
    }
    lab_game_sound_end();
    sample.valid = false;
    lab_motion_publish(&sample);
    for (unsigned i = 0; i < 3; ++i) {
        rt_err_t restore = lab_reg_write(bus, 0x6A, 0x10 + i, old[i]);
        if (!err) err = restore;
    }
    if (err) lab_error(LAB_GAME, "Game IMU / restore", err);
    else lab_publish(LAB_GAME, LAB_OBSERVE, "Ride finished", "IMU settings restored. Confirm motion and display response on the board.");
}
