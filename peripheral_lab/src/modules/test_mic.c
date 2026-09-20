/*
 * test_mic.c — 麦克风测试：通过音频回调保存 PCM 并统计数字电平，支持原声与增强回放。
 */
#include "lab_audio.h"
#include "lab_units.h"
#include "audio_server.h"
#include <math.h>
#include <string.h>

#define RECORD_SAMPLES (LAB_AUDIO_RATE * 3)
static int16_t *recording;
static unsigned recorded;
static struct rt_mutex mic_lock;
static bool initialized;
static struct { unsigned count, clipped, peak; int64_t sum; uint64_t squares; } level;

/* 音频服务回调：在锁内累计电平并保存有限长度 PCM，不操作界面。 */
static int mic_callback(audio_server_callback_cmt_t cmd, void *user, uint32_t reserved)
{
    (void)user;
    if (cmd != as_callback_cmd_data_coming) return 0;
    const audio_server_coming_data_t *data = (const void *)(uintptr_t)reserved;
    if (!data || !data->data || (data->data_len & 1)) return 0;
    rt_mutex_take(&mic_lock, RT_WAITING_FOREVER);
    unsigned n = data->data_len / 2;
    const int16_t *pcm = (const int16_t *)data->data;
    for (unsigned i = 0; i < n; ++i) {
        int32_t sample = pcm[i];
        unsigned magnitude = sample < 0 ? -sample : sample;
        level.count++; level.sum += sample;
        level.squares += (int64_t)sample * sample;
        if (magnitude > level.peak) level.peak = magnitude;
        if (magnitude >= 32760) level.clipped++;
    }
    unsigned keep = RECORD_SAMPLES - recorded;
    if (keep > n) keep = n;
    if (keep) { memcpy(recording + recorded, pcm, keep * 2); recorded += keep; }
    rt_mutex_release(&mic_lock);
    return 0;
}
/* 按动作录音或回放；录音结束关闭输入客户端后才允许使用录音缓冲回放。 */
void lab_mic_run(unsigned action)
{
    if (action == 1 || action == 2) { lab_audio_play_gain(LAB_MIC, recording, recorded, 0, action == 2 ? 8 : 1); return; }
    if (!initialized) {
        if (rt_mutex_init(&mic_lock, "lab_mic", RT_IPC_FLAG_PRIO) != RT_EOK) { lab_error(LAB_MIC, "Mic lock", -RT_ERROR); return; }
        initialized = true;
    }
    if (!recording) recording = rt_malloc(RECORD_SAMPLES * sizeof(int16_t));
    if (!recording) { lab_error(LAB_MIC, "Allocate recording RAM", -RT_ENOMEM); return; }
    recorded = 0;
    memset(&level, 0, sizeof(level));
    audio_parameter_t param = {0};
    param.read_samplerate = LAB_AUDIO_RATE;
    param.read_channnel_num = 1;
    param.read_bits_per_sample = 16;
    param.read_which_mic = AUDIO_MIC0_ONLY;
    param.read_cache_size = 2048;
    /* 保留原始电平用于诊断；不启用 3A/AGC，增强仅在回放时应用。 */
    param.disable_uplink_agc = 1;
    /* 以 AUDIO_RX 打开录音通路，后续 PCM 由音频服务交给 mic_callback。 */
    audio_client_t client = audio_open2(AUDIO_TYPE_LOCAL_RECORD, AUDIO_RX, &param, mic_callback, NULL, AUDIO_DEVICE_SPEAKER);
    if (!client) { lab_error(LAB_MIC, "Open microphone", -RT_ERROR); return; }
    unsigned total = 0, peak = 0, clip = 0;
    char value[80], detail[192], db_text[24];
    for (unsigned elapsed = 0; elapsed < 3200 && !lab_cancelled(); elapsed += 200) {
        rt_thread_mdelay(200);
        rt_mutex_take(&mic_lock, RT_WAITING_FOREVER);
        unsigned count = level.count;
        if (level.peak > peak) peak = level.peak;
        clip += level.clipped; total += count;
        /* 用均方减去均值平方去除直流分量，再计算 RMS/dBFS 数字电平。 */
        double mean = count ? (double)level.sum / count : 0;
        double variance = count ? (double)level.squares / count - mean * mean : 0;
        memset(&level, 0, sizeof(level));
        rt_mutex_release(&mic_lock);
        float rms = variance > 0 ? sqrtf((float)variance) : 0;
        int32_t milli_db = rms > 0 ? (int32_t)(20000.0f * log10f(rms / 32768.0f)) : -96000;
        lab_fixed3(db_text, sizeof(db_text), milli_db);
        rt_snprintf(value, sizeof(value), "%s dBFS", db_text);
        rt_snprintf(detail, sizeof(detail), "Recording: %u ms / 3000 ms\nRaw peak %u, RMS %u, clipped %u\n16 kHz / mono / 16-bit. Speak or clap; not calibrated dB SPL.", total * 1000 / LAB_AUDIO_RATE, peak, (unsigned)rms, clip);
        lab_publish(LAB_MIC, LAB_RUNNING, value, detail);
        if (total >= RECORD_SAMPLES) break;
    }
    int closed = audio_close(client); /* 返回后 SDK 不再回调，录音缓冲可用于回放。 */
    if (closed) lab_error(LAB_MIC, "Close microphone", closed);
    else if (lab_cancelled()) lab_publish(LAB_MIC, LAB_IDLE, "Recording stopped", "Partial recording kept in RAM. Use Play original to listen.");
    else if (recorded < LAB_AUDIO_RATE * 5 / 2 || !peak) lab_publish(LAB_MIC, LAB_FAIL, "No usable mic data", "Insufficient samples or all-zero PCM. Check mic power and codec input.");
    else {
        rt_snprintf(detail, sizeof(detail), "Raw peak %u; clipped %u. Compare Play original / Play +18 dB. Repeat at the same distance; watch raw RMS/dBFS while speaking vs silence. PCM kept in RAM.", peak, clip);
        lab_publish(LAB_MIC, LAB_OBSERVE, "Recording ready", detail);
    }
}
