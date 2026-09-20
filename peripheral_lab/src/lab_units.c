/*
 * lab_units.c — 传感器单位换算：将原始计数转换为物理量，并用整数输出三位小数。
 */
#include "lab_units.h"
#include <rtthread.h>

static int32_t rounded(int64_t n, int32_t d) { return (int32_t)((n + (n < 0 ? -d/2 : d/2)) / d); }
/* LSM6DSL/LSM6DS3TR-C: +/-2 g, +/-245 dps，分别为 0.061 mg/LSB、8.75 mdps/LSB。 */
int32_t lab_accel_millig(int16_t raw) { return rounded((int32_t)raw * 61, 1000); }
int32_t lab_gyro_millidps(int16_t raw) { return rounded((int32_t)raw * 875, 100); }
/* MMC5603 20 位输出：16384 count/G，1 G = 100 uT。 */
int32_t lab_mag_milliut(int32_t raw) { return rounded((int64_t)raw * 100000, 16384); }
/* 将千分之一单位的整数格式化为三位小数，保留负号。 */
void lab_fixed3(char *text, size_t size, int32_t milli)
{
    uint32_t magnitude = milli < 0 ? (uint32_t)(-(int64_t)milli) : (uint32_t)milli;
    rt_snprintf(text, size, "%s%lu.%03lu", milli < 0 ? "-" : "", (unsigned long)(magnitude / 1000), (unsigned long)(magnitude % 1000));
}
/* 根据双通道比例估算照度；饱和、保留增益或无效光谱比例不输出估算值。 */
bool lab_lux_milli(uint16_t ch0, uint16_t ch1, uint8_t gain_code, uint8_t rate, int32_t *lux)
{
    static const unsigned gains[] = {1, 2, 4, 8, 0, 0, 48, 96};
    static const unsigned integration[] = {100, 50, 200, 400, 150, 250, 300, 350};
    static const unsigned repeat[] = {50, 100, 200, 500, 1000, 2000, 2000, 2000};
    unsigned gain = gains[gain_code & 7], ms = integration[(rate >> 3) & 7];
    if (!gain || ch0 == 65535 || ch1 == 65535) return false;
    if (ms > repeat[rate & 7]) ms = repeat[rate & 7];
    uint32_t sum = (uint32_t)ch0 + ch1;
    if (!sum) { *lux = 0; return true; }
    int64_t weighted;
    /* Lite-On LTR303/329 Appendix A，按 CH1/(CH0+CH1) 分段。 */
    if ((uint32_t)ch1 * 100 < sum * 45) weighted = 17743LL * ch0 + 11059LL * ch1;
    else if ((uint32_t)ch1 * 100 < sum * 64) weighted = 42785LL * ch0 - 19548LL * ch1;
    else if ((uint32_t)ch1 * 100 < sum * 85) weighted = 5926LL * ch0 + 1185LL * ch1;
    else return false; /* 超出公式有效光谱比例，不把强红外误显示为黑暗。 */
    *lux = rounded(weighted * 10, gain * ms);
    return true;
}
