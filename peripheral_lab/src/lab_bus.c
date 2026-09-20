/*
 * lab_bus.c — I2C 公共访问层：封装总线查找、寄存器读写和有超时的数据就绪轮询。
 */
#include "lab.h"
#include "bf0_hal.h"
#include "drv_io.h"
#include <string.h>

/* 查找 BSP 注册的 I2C 总线；I2C3 使用本板 PA40/PA39 的引脚复用。 */
struct rt_i2c_bus_device *lab_i2c(const char *name)
{
    if (strcmp(name, "i2c3") == 0) {
        HAL_PIN_Set(PAD_PA40, I2C3_SCL, PIN_PULLUP, 1);
        HAL_PIN_Set(PAD_PA39, I2C3_SDA, PIN_PULLUP, 1);
    }
    return rt_i2c_bus_device_find(name);
}

/* 读取连续寄存器，只有实际读到全部字节才返回成功。 */
rt_err_t lab_reg_read(struct rt_i2c_bus_device *bus, uint8_t addr, uint8_t reg,
                     void *data, uint16_t size)
{
    if (!bus) return -RT_ENOSYS;
    /* SiFli 的 MEM_ACCESS 才保证寄存器地址与数据之间使用 repeated START。 */
    return rt_i2c_mem_read(bus, addr, reg, 8, data, size) == size ? RT_EOK : -RT_EIO;
}

/* 把寄存器地址和一个数据字节作为同一条 I2C 写消息发送。 */
rt_err_t lab_reg_write(struct rt_i2c_bus_device *bus, uint8_t addr, uint8_t reg, uint8_t value)
{
    uint8_t bytes[2] = {reg, value};
    struct rt_i2c_msg msg = { .addr = addr, .flags = RT_I2C_WR, .len = 2, .buf = bytes };
    if (!bus) return -RT_ENOSYS;
    return rt_i2c_transfer(bus, &msg, 1) == 1 ? RT_EOK : -RT_EIO;
}

/* 定期检查状态位是否全部置位；读失败或超时均结束等待。 */
bool lab_poll_reg(struct rt_i2c_bus_device *bus, uint8_t addr, uint8_t reg,
                  uint8_t mask, unsigned timeout_ms)
{
    rt_tick_t start = rt_tick_get();
    do {
        uint8_t value = 0;
        if (lab_reg_read(bus, addr, reg, &value, 1) != RT_EOK) return false;
        if ((value & mask) == mask) return true;
        rt_thread_mdelay(10);
    } while ((rt_tick_t)(rt_tick_get() - start) < rt_tick_from_millisecond(timeout_ms));
    return false;
}
