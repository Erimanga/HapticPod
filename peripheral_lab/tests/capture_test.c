#include "lab_touch_capture.h"
#include <rthw.h>
#include <assert.h>
#include <string.h>
static struct rt_i2c_bus_device bus = {1}, other = {2};
static unsigned transfers;
struct rt_i2c_bus_device *rt_i2c_bus_device_find(const char *name) { assert(!strcmp(name, "i2c1")); return &bus; }
rt_base_t rt_hw_interrupt_disable(void) { return 0; }
void rt_hw_interrupt_enable(rt_base_t l) { (void)l; }
rt_tick_t rt_tick_get(void) { return 123; }
rt_size_t __real_rt_i2c_transfer(struct rt_i2c_bus_device *b, struct rt_i2c_msg *m, rt_uint32_t n)
{ (void)b; (void)m; transfers++; return n; }
extern rt_size_t __wrap_rt_i2c_transfer(struct rt_i2c_bus_device *, struct rt_i2c_msg *, rt_uint32_t);
int main(void)
{
    uint8_t reg = 1, data[14] = {0, 2, 0, 100, 0, 120, 0, 0, 0x80, 200, 0x10, 220, 0, 0};
    struct rt_i2c_msg m[] = {{.addr=0x38,.len=1,.buf=&reg}, {.addr=0x38,.flags=RT_I2C_RD,.len=14,.buf=data}};
    lab_touch_capture_t sample;
    lab_touch_capture_enable(true);
    assert(__wrap_rt_i2c_transfer(&bus, m, 2) == 2 && transfers == 1);
    lab_touch_capture_snapshot(&sample);
    assert(sample.sequence == 1 && sample.tick == 123 && !memcmp(sample.data, data + 1, 13));
    lab_touch_frame_t frame; assert(lab_touch_decode(sample.data, &frame) && frame.count == 2);
    __wrap_rt_i2c_transfer(&other, m, 2); reg = 0xA3; __wrap_rt_i2c_transfer(&bus, m, 2);
    lab_touch_capture_snapshot(&sample); assert(sample.sequence == 1 && transfers == 3);
    lab_touch_capture_enable(false); reg = 1; __wrap_rt_i2c_transfer(&bus, m, 2);
    lab_touch_capture_snapshot(&sample); assert(sample.sequence == 0 && transfers == 4);
    puts("PASS: SDK frame observation captures both contacts with exactly one underlying transfer; unrelated reads remain untouched.");
}
