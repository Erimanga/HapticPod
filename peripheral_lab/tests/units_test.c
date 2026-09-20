#include "lab_units.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    assert(lab_accel_millig(16384) == 999);
    assert(lab_accel_millig(-16384) == -999);
    assert(lab_gyro_millidps(1000) == 8750);
    assert(lab_gyro_millidps(-1000) == -8750);
    assert(lab_mag_milliut(16384) == 100000);
    assert(lab_mag_milliut(-524288) == -3200000);
    char number[24]; lab_fixed3(number, sizeof(number), -1); assert(!strcmp(number, "-0.001"));
    int32_t lux;
    assert(lab_lux_milli(1000, 0, 0, 3, &lux) && lux == 1774300);
    assert(lab_lux_milli(1000, 0, 1, 3, &lux) && lux == 887150);
    assert(lab_lux_milli(1000, 0, 0, 0x12, &lux) && lux == 887150);
    assert(lab_lux_milli(1000, 0, 0, 0, &lux) && lux == 3548600); /* integration clamped to repeat */
    assert(lab_lux_milli(500, 500, 0, 3, &lux) && lux == 1161850);
    assert(lab_lux_milli(200, 800, 0, 3, &lux) && lux == 213320);
    assert(lab_lux_milli(0, 0, 7, 3, &lux) && lux == 0);
    assert(!lab_lux_milli(0, 1000, 0, 3, &lux)); /* IR dominant */
    assert(!lab_lux_milli(65535, 10, 0, 3, &lux));
    assert(!lab_lux_milli(100, 10, 4, 3, &lux));
    puts("PASS: signed physical units, LTR303 gain/integration/ratio boundaries and invalid conversions.");
}
