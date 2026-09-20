/*
 * lab_pan_audio_compat.c — SDK 链接兼容层：仅在未启用蓝牙音频时补齐语音空入口，不实现音频传输。
 */
#include <rtthread.h>
#include <stdint.h>
/* SDK 2.5.1 audioproc.h 用 BT_FINSH 判断语音支持，PAN 也开启该宏。
 * 本项目禁用 HFP/蓝牙音频，仅补齐 audio_server 引用的两个空处理入口。
 * 启用蓝牙音频后必须由 SDK 提供实现，不允许此适配静默替代。 */
#if defined(BT_FINSH) && !defined(AUDIO_BT_AUDIO) && !defined(CFG_HFP_HF) && !defined(CFG_HFP_AG)
void bt_voice_downlink_process(uint8_t ready) { (void)ready; }
void bt_voice_uplink_send(void) { }
#endif
