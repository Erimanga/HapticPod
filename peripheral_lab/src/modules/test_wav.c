/*
 * test_wav.c — 内置 WAV 测试：音频存于 Flash，校验格式后交给通用 PCM 播放器。
 */
#include "lab_audio.h"
#include "lab_wav.h"
static const uint8_t sample_wav[] __attribute__((aligned(4))) = {
#include "lab_wav_sample.inc"
};
/* 检查 Flash 中的 WAV，直接引用其中 PCM，避免再分配整段音频缓冲。 */
void lab_wav_run(unsigned volume)
{
    const uint8_t *pcm;
    unsigned samples;
    if (!lab_wav_decode(sample_wav, sizeof(sample_wav), &pcm, &samples)) {
        lab_error(LAB_SPEAKER, "WAV must be 16 kHz / mono / PCM16", -RT_ERROR);
        return;
    }
    rt_kprintf("[lab:wav] RIFF PCM: 16000 Hz, mono, 16 bit, %u samples, volume %u/15\n", samples, volume);
    lab_audio_play_volume(LAB_SPEAKER, (const int16_t *)pcm, samples, 0, 1, volume);
}
