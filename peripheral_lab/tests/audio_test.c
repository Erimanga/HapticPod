#include "lab_audio.h"
#include "lab_wav.h"
#include "audio_server.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static unsigned ticks, writes, bytes, peak, closed, volume = 9, expected_volume = 6;
static bool stall, cancel, reject_open;
static const int16_t *expected_pcm;
static unsigned pcm_index;
static lab_status_t final;
static char final_detail[192];
rt_tick_t rt_tick_get(void) { return ticks; }
rt_tick_t rt_tick_from_millisecond(unsigned ms) { return ms; }
void rt_thread_mdelay(unsigned ms) { ticks += ms; }
bool lab_cancelled(void) { return cancel && ticks >= 100; }
void lab_publish(lab_id_t id, lab_status_t state, const char *v, const char *d)
{ (void)id; (void)v; final = state; snprintf(final_detail, sizeof(final_detail), "%s", d); }
void lab_error(lab_id_t id, const char *op, int code) { (void)id; (void)op; (void)code; final = LAB_FAIL; }
uint8_t audio_server_get_private_volume(audio_type_t t) { (void)t; return volume; }
int audio_server_set_private_volume(audio_type_t t, uint8_t v) { (void)t; volume = v; return 0; }
audio_client_t audio_open2(audio_type_t type, audio_rwflag_t rw, audio_parameter_t *p, audio_server_callback_func cb, void *user, audio_device_e dev)
{
    (void)cb; (void)user;
    assert(type == AUDIO_TYPE_LOCAL_MUSIC && rw == AUDIO_TX && dev == AUDIO_DEVICE_SPEAKER);
    assert(p->write_samplerate == 16000 && p->write_channnel_num == 1 && p->write_bits_per_sample == 16);
    assert(volume == expected_volume);
    return reject_open ? NULL : (void *)1;
}
int audio_write(audio_client_t client, uint8_t *data, uint32_t len)
{
    assert(client && !(len & 1)); writes++;
    if (stall || writes % 5 == 0) return 0;
    unsigned accepted = len > 4 ? (len / 2) & ~1u : len;
    int16_t *pcm = (int16_t *)data;
    for (unsigned i = 0; i < accepted / 2; ++i) {
        if (expected_pcm) assert(pcm[i] == expected_pcm[pcm_index++]);
        unsigned mag = abs(pcm[i]); if (mag > peak) peak = mag;
    }
    bytes += accepted; return accepted;
}
int audio_ioctl(audio_client_t c, int cmd, void *p) { assert(c && cmd == AUDIO_IOCTL_FLUSH_TIME_MS); *(uint32_t *)p = 20; return 0; }
int audio_close(audio_client_t c) { assert(c); closed++; return 0; }
static void reset(void) { expected_volume = 6; expected_pcm = NULL; pcm_index = 0; ticks = writes = bytes = peak = closed = 0; stall = cancel = reject_open = false; final = LAB_IDLE; volume = 9; }
int main(void)
{
    reset(); lab_audio_play(LAB_SPEAKER, NULL, 32000, 440);
    assert(final == LAB_OBSERVE && bytes == 64000 && closed == 1 && volume == 9 && peak > 5000 && peak <= 6000);
    reset(); stall = true; lab_audio_play(LAB_SPEAKER, NULL, 32000, 1000);
    assert(final == LAB_FAIL && closed == 1 && volume == 9 && ticks < 2000);
    reset(); cancel = true; lab_audio_play(LAB_SPEAKER, NULL, 32000, 1000);
    assert(final == LAB_IDLE && closed == 1 && volume == 9 && bytes < 64000);
    reset(); reject_open = true; lab_audio_play(LAB_SPEAKER, NULL, 32000, 1000);
    assert(final == LAB_FAIL && closed == 0 && volume == 9);
    reset(); lab_audio_play(LAB_MIC, NULL, 0, 0);
    assert(final == LAB_FAIL && closed == 0);
    reset(); expected_volume = 0;
    lab_audio_play_volume(LAB_SPEAKER, NULL, 32000, 100, 1, 0);
    assert(final == LAB_OBSERVE && volume == 9);
    reset(); expected_volume = 15;
    lab_audio_play_volume(LAB_SPEAKER, NULL, 32000, 4000, 1, 15);
    assert(final == LAB_OBSERVE && volume == 9);
    reset(); lab_audio_play_volume(LAB_SPEAKER, NULL, 32000, 440, 1, 16);
    assert(final == LAB_FAIL && closed == 0 && volume == 9);
    reset(); expected_volume = 2;
    lab_wav_run(2);
    assert(final == LAB_OBSERVE && volume == 9 && bytes == 64000 && peak > 1000);
    assert(strstr(final_detail, "WAV played at volume 2/15"));
    reset(); cancel = true;
    lab_wav_run(6);
    assert(final == LAB_IDLE && volume == 9 && closed == 1 && bytes < 64000);
    const int16_t original[] = {-32768, -5000, -100, 0, 100, 5000, 32767};
    const int16_t boosted[] = {-32768, -32768, -800, 0, 800, 32767, 32767};
    reset(); expected_pcm = boosted;
    lab_audio_play_gain(LAB_MIC, original, 7, 0, 8);
    assert(final == LAB_OBSERVE && pcm_index == 7 && volume == 9);
    assert(strstr(final_detail, "clipped 4/7 samples"));
    reset(); expected_pcm = original;
    lab_audio_play(LAB_MIC, original, 7, 0);
    assert(final == LAB_OBSERVE && pcm_index == 7);
    puts("PASS: saturating gain and unchanged raw PCM; PCM partial writes/backpressure, timeout, cancellation, volume restoration, open failure and no-recording guard.");
}
