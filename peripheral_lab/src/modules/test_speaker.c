/*
 * test_speaker.c — 扬声器测试入口：按按钮选择音阶、自定义频率或 WAV，再调用统一播放接口。
 */
#include "lab_audio.h"
#include "lab_wav.h"
/* 将界面动作映射为音阶、自定义音调或内置 WAV 播放。 */
void lab_speaker_run(unsigned action)
{
    unsigned volume, frequency;
    lab_speaker_settings(&volume, &frequency);
    if (action == 9) { lab_wav_run(volume); return; }
    if (action < 8) frequency = lab_speaker_notes[action];
    lab_audio_play_volume(LAB_SPEAKER, NULL, LAB_AUDIO_RATE * 2, frequency, 1, volume);
}
