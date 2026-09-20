/*
 * lab_units.h — 单位换算接口：供传感器模块复用，不负责硬件采集或整板校准。
 */
#ifndef LAB_UNITS_H
#define LAB_UNITS_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
/* 返回物理量的千分之一单位，避免依赖嵌入式 printf 浮点支持。 */
int32_t lab_accel_millig(int16_t raw);
int32_t lab_gyro_millidps(int16_t raw);
int32_t lab_mag_milliut(int32_t centered_raw);
bool lab_lux_milli(uint16_t ch0, uint16_t ch1, uint8_t gain_code, uint8_t rate, int32_t *lux);
void lab_fixed3(char *text, size_t size, int32_t milli);
#endif
