/*
 * lab_audio.h — 音频测试接口：由外设工作线程调用，统一回放录音、WAV 和正弦测试音。
 */
#ifndef LAB_AUDIO_H
#define LAB_AUDIO_H
#include "lab.h"
#define LAB_AUDIO_RATE 16000
/* pcm 为 NULL 时生成 2 秒测试音；否则回放指定的单声道 16 位 PCM。 */
void lab_audio_play(lab_id_t id, const int16_t *pcm, unsigned samples, unsigned frequency);
/* 仅回放时放大并饱和限幅，原始录音不变；gain 为 1 或 8。 */
void lab_audio_play_gain(lab_id_t id, const int16_t *pcm, unsigned samples, unsigned frequency, unsigned gain);
/* 播放结束恢复原音量，volume 取 0..15。 */
void lab_audio_play_volume(lab_id_t id, const int16_t *pcm, unsigned samples, unsigned frequency, unsigned gain, unsigned volume);
#endif
