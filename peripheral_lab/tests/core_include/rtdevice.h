#ifndef LAB_TEST_DEVICES_H
#define LAB_TEST_DEVICES_H
#include <stdint.h>
#define RT_I2C_RD 1
struct rt_i2c_bus_device { int id; };
struct rt_i2c_msg { uint16_t addr, mem_addr, mem_addr_size, flags, len; uint8_t *buf; };
struct rt_i2c_bus_device *rt_i2c_bus_device_find(const char *name);
#endif
