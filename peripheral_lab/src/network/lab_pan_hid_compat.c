/*
 * lab_pan_hid_compat.c — HID 兼容层：保留辅助连接所需应答，避免 SDK 自动鼠标报告干扰手机。
 */
#include "bts2_app_inc.h"
#include <string.h>
/* SDK HID 连接时会自动校准鼠标。联网对照测试屏蔽主动输入，
 * 保留控制通道应答；GET_REPORT 的 Input 数据返回全零。 */
void __real_hid_send_report_req_ext(U16 tid,BTS2S_BD_ADDR *bd,U16 len,U8 *data,BOOL interrupt);
void __wrap_hid_send_report_req_ext(U16 tid,BTS2S_BD_ADDR *bd,U16 len,U8 *data,BOOL interrupt)
{
    if(interrupt) return;
    if(data && len>=2 && data[0]==0xa1) {
        U8 neutral[256];
        if(len>sizeof(neutral)) return;
        memcpy(neutral,data,2); memset(neutral+2,0,len-2);
        __real_hid_send_report_req_ext(tid,bd,len,neutral,interrupt);
    } else __real_hid_send_report_req_ext(tid,bd,len,data,interrupt);
}
