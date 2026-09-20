/*
 * lab_audio.c — 通用音频回放：分块提交 PCM 或测试音，处理停止、写入等待和音量恢复。
 */
#include "lab_audio.h"
#include "audio_server.h"
#include <math.h>
#include <string.h>

/* RIFF 数据可能不对齐；通过 memcpy 读取，SiFli/主机测试均为小端。 */
static int16_t pcm_sample(const int16_t *pcm, unsigned index)
{
    int16_t sample;
    memcpy(&sample, (const uint8_t *)pcm + index * 2, sizeof(sample));
    return sample;
}

/* 以原始增益播放 PCM；没有 PCM 时根据指定频率生成测试音。 */
void lab_audio_play(lab_id_t id, const int16_t *pcm, unsigned samples, unsigned frequency)
{
    lab_audio_play_gain(id, pcm, samples, frequency, 1);
}

/* 使用默认测试音量播放，可对录音施加回放增益。 */
void lab_audio_play_gain(lab_id_t id, const int16_t *pcm, unsigned samples, unsigned frequency, unsigned gain)
{
    lab_audio_play_volume(id, pcm, samples, frequency, gain, 6);
}

/* 打开音频输出，分块播放并响应停止，最后关闭客户端、恢复原音量。 */
void lab_audio_play_volume(lab_id_t id, const int16_t *pcm, unsigned samples, unsigned frequency, unsigned gain, unsigned volume)
{
    if (volume > 15) { lab_error(id, "Invalid volume", -RT_ERROR); return; }
    if (gain != 1 && gain != 8) { lab_error(id, "Invalid PCM gain", -RT_ERROR); return; }
    if (!samples) { lab_error(id, "No recording; record first", -RT_EEMPTY); return; }
    audio_parameter_t param = {0};
    param.write_samplerate = LAB_AUDIO_RATE;
    param.write_channnel_num = 1;
    param.write_bits_per_sample = 16;
    param.write_cache_size = 4096;
    uint8_t old_volume = audio_server_get_private_volume(AUDIO_TYPE_LOCAL_MUSIC);
    if (audio_server_set_private_volume(AUDIO_TYPE_LOCAL_MUSIC, volume) != 0) {
        lab_error(id, "Set playback volume", -RT_ERROR); return;
    }
    /* 以 AUDIO_TX 打开播放通路；打开失败也恢复此前音量。 */
    audio_client_t client = audio_open2(AUDIO_TYPE_LOCAL_MUSIC, AUDIO_TX, &param, NULL, NULL, AUDIO_DEVICE_SPEAKER);
    if (!client) {
        audio_server_set_private_volume(AUDIO_TYPE_LOCAL_MUSIC, old_volume);
        lab_error(id, "Open speaker", -RT_ERROR); return;
    }
    char detail[192];
    if (pcm && id == LAB_SPEAKER) rt_snprintf(detail, sizeof(detail), "WAV: PCM16 / mono / 16000 Hz / %u ms. Volume %u/15. STOP TEST stops playback.", (unsigned)((uint64_t)samples * 1000 / LAB_AUDIO_RATE), volume);
    else if (pcm) rt_snprintf(detail, sizeof(detail), "Playback gain x%u (%s). Raw recording unchanged. Volume %u/15. STOP TEST stops playback.", gain, gain == 8 ? "+18 dB" : "0 dB", volume);
    else rt_snprintf(detail, sizeof(detail), "%u Hz / 2 seconds. Volume %u/15. STOP TEST stops playback.", frequency, volume);
    lab_publish(id, LAB_RUNNING, pcm ? (id == LAB_SPEAKER ? "Playing WAV" : "Playing recording") : "Playing test tone", detail);
    unsigned offset = 0, clipped = 0;
    int error = 0;
    int16_t block[256];
    rt_tick_t last_progress = rt_tick_get();
    while (offset < samples && !lab_cancelled()) {
        unsigned n = samples - offset;
        if (n > 256) n = 256;
        for (unsigned i = 0; i < n; ++i) {
            if (pcm) {
                int32_t sample = (int32_t)pcm_sample(pcm, offset + i) * (int32_t)gain;
                block[i] = sample > 32767 ? 32767 : sample < -32768 ? -32768 : sample;
            }
            else {
                /* 低幅度正弦，起止 20 ms 淡入淡出，减少爆音。 */
                unsigned pos = offset + i, ramp = pos < 320 ? pos : samples - pos < 320 ? samples - pos : 320;
                block[i] = (int16_t)(6000.0f * (float)ramp / 320.0f * sinf(6.283185307f * ((pos * frequency) % LAB_AUDIO_RATE) / LAB_AUDIO_RATE));
            }
        }
        /* 写入可能只接受部分 PCM 或暂时返回 0，按实际字节数推进，不能直接跳到下一块。 */
        int wrote = audio_write(client, (uint8_t *)block, n * 2);
        if (wrote < 0 || wrote > (int)n*2 || (wrote & 1)) { error = -RT_EIO; break; }
        if (wrote) {
            /* 只统计实际被接收的样本，重试不重复计数。 */
            if (pcm) for (unsigned i = 0; i < (unsigned)wrote / 2; ++i) {
                int32_t sample = (int32_t)pcm_sample(pcm, offset + i) * (int32_t)gain;
                if (sample > 32767 || sample < -32768) clipped++;
            }
            offset += wrote / 2; last_progress = rt_tick_get();
        }
        else if ((rt_tick_t)(rt_tick_get() - last_progress) > rt_tick_from_millisecond(1500)) { error = -RT_ETIMEOUT; break; }
        rt_thread_mdelay(10);
    }
    /* 提交完成后等待剩余缓存播放，避免立即关闭导致尾音截断。 */
    if (!error && !lab_cancelled()) {
        uint32_t drain = 0;
        if (audio_ioctl(client, AUDIO_IOCTL_FLUSH_TIME_MS, &drain) != 0 || drain > 1000) error = -RT_EIO;
        else for (unsigned elapsed = 0; elapsed < drain + 120 && !lab_cancelled(); elapsed += 10) rt_thread_mdelay(10);
    }
    int closed = audio_close(client);
    audio_server_set_private_volume(AUDIO_TYPE_LOCAL_MUSIC, old_volume);
    if (error || closed) lab_error(id, "Playback / close", error ? error : closed);
    else if (lab_cancelled()) lab_publish(id, LAB_IDLE, "Playback stopped", "Audio client closed; volume restored.");
    else {
        if (pcm && id == LAB_SPEAKER) rt_snprintf(detail, sizeof(detail), "WAV played at volume %u/15. Did you hear the complete melody clearly? Confirm below.", volume);
        else if (pcm) rt_snprintf(detail, sizeof(detail), "Gain x%u; clipped %u/%u samples. Compare with original; boost amplifies noise too. Confirm if your voice is clear.", gain, clipped, samples);
        else rt_snprintf(detail, sizeof(detail), "%u Hz / volume %u/15 complete. Did you hear clear audio without distortion? Confirm below.", frequency, volume);
        lab_publish(id, LAB_OBSERVE, pcm ? (id == LAB_SPEAKER ? "WAV complete" : "Recording played") : "Tone complete", detail);
    }
}
