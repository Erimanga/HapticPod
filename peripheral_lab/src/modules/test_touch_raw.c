/*
 * test_touch_raw.c — 双点触摸诊断：观察 SDK 原始帧，验证两个不同触点持续出现，不额外抢读触摸数据。
 */
#include "lab.h"
#include "lab_touch_capture.h"

/* 检查触摸标识并观察 SDK 帧，两个有效触点连续保持 250 ms 才判定通过。 */
void lab_touch_run(unsigned action)
{
    (void)action;
    struct rt_i2c_bus_device *bus = lab_i2c("i2c1");
    lab_touch_frame_t frame = {0};
    lab_touch_capture_t sample = {0};
    bool verified = false, holding_two = false;
    unsigned maximum = 0;
    rt_tick_t start = rt_tick_get(), last_log = start, both_since = start;
    uint32_t previous = 0;
    uint8_t chip = 0, firmware = 0;
    rt_err_t err = lab_reg_read(bus, 0x38, 0xA3, &chip, 1);
    if (err == RT_EOK) err = lab_reg_read(bus, 0x38, 0xA6, &firmware, 1);
    if (err != RT_EOK) { lab_error(LAB_TOUCH, "Read touch identity", err); return; }
    lab_touch_capture_enable(true);
    lab_touch_frame_publish(&frame);
    lab_publish(LAB_TOUCH, LAB_RUNNING, "Place two fingers", "Watching SDK frames, without a second I2C reader. Move both fingers slightly.");
    while (!lab_cancelled()) {
        lab_touch_capture_snapshot(&sample);
        rt_tick_t now = rt_tick_get();
        if (sample.sequence != previous) {
            previous = sample.sequence;
            unsigned reported = sample.data[0] & 15;
            if (reported > maximum) maximum = reported;
            bool valid = lab_touch_decode(sample.data, &frame);
            if (!valid) frame.count = 0;
            lab_touch_frame_publish(&frame);
            if (valid && frame.count == 2) {
                if (!holding_two) { holding_two = true; both_since = sample.tick; }
                if ((rt_tick_t)(sample.tick - both_since) >= rt_tick_from_millisecond(250)) verified = true;
            } else holding_two = false;
        }
        if (!sample.sequence || (rt_tick_t)(now - sample.tick) > rt_tick_from_millisecond(400)) {
            holding_two = false;
            frame.count = 0;
            lab_touch_frame_publish(&frame);
        }
        if ((rt_tick_t)(now - last_log) >= rt_tick_from_millisecond(1000)) {
            char value[80], detail[192];
            rt_snprintf(value, sizeof(value), verified ? "Two contacts verified" : "Checking real contact count");
            rt_snprintf(detail, sizeof(detail), "Chip %02X / FW %02X. Reported %u, decoded %u, max %u.\n%s", chip, firmware, sample.data[0] & 15, frame.count, maximum,
                        maximum < 2 ? "If two fingers still report 1, panel/firmware capability needs checking. No synthetic second point." : "Two-point packets seen. Move fingers to compare positions.");
            lab_publish(LAB_TOUCH, verified ? LAB_PASS : LAB_RUNNING, value, detail);
            rt_kprintf("[lab:touch-frame] seq=%lu raw:", (unsigned long)sample.sequence);
            for (unsigned i = 0; i < sizeof(sample.data); ++i) rt_kprintf(" %02X", sample.data[i]);
            rt_kprintf("\n");
            last_log = now;
        }
        if (!sample.sequence && (rt_tick_t)(now - start) > rt_tick_from_millisecond(5000)) {
            lab_publish(LAB_TOUCH, LAB_FAIL, "No SDK touch frames", "No captured frame in 5 seconds. Touch and move on the panel, then retry; check SDK driver / capture hook.");
            break;
        }
        rt_thread_mdelay(50);
    }
    lab_touch_capture_enable(false);
    frame.count = 0;
    lab_touch_frame_publish(&frame);
    lab_result_t final;
    lab_snapshot(LAB_TOUCH, &final);
    if (final.status != LAB_FAIL) {
        char detail[192];
        rt_snprintf(detail, sizeof(detail), "Chip %02X / FW %02X / max reported %u. %s", chip, firmware, maximum,
                    verified ? "Two distinct IDs verified in SDK frames." : "Two-point support not verified. Keep raw frame logs to distinguish panel firmware from driver issues.");
        lab_publish(LAB_TOUCH, verified ? LAB_PASS : LAB_FAIL, verified ? "Two fingers verified" : "Two points not observed", detail);
    }
}
