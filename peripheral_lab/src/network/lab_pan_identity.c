/*
 * lab_pan_identity.c — 经典蓝牙设备身份：向 SDK 提供本项目使用的网络/LAN 设备类别。
 */
#include "bts2_app_inc.h"
/* 恢复先前 HID 测试版本的 LAN/Networking 类别，便于相同条件复测。 */
uint32_t bt_get_class_of_device(void)
{
    return BT_SRVCLS_NETWORK | BT_DEVCLS_LAP | BT_LAP_FULLY;
}
/* EIR 由 set_local_name 写入名称，与先前 HID 版本一致。 */
