/*
 * lab_game_sound.c — 游戏音效：独立任务从 Flash PCM 分块送音，用单槽待播消息和会话编号控制取消。
 */
#include "lab.h"
#include "lab_game_sound.h"
#include "audio_server.h"
#include "lab_game_pcm.h"
#include <string.h>

#define SOUND_BLOCK_SAMPLES (SOUND_SAMPLE_RATE / 100) /* 10 ms */
#define SOUND_SAMPLES_PER_MS (SOUND_SAMPLE_RATE / 1000)
#define SOUND_DRAIN_MARGIN_MS 200
#define SOUND_THREAD_PRIORITY 18

/* Single bounded mailbox: never accumulate sound behind the game. No LVGL or
 * sensor I/O on this thread. lab_io waits for closure before releasing busy. */
static struct rt_mutex lock;
static bool initialized, active, running, available;
static unsigned generation;
static unsigned game_volume=6;
static game_sound_t pending;
/* 建立音效状态锁；失败时保留游戏其他功能。 */
void lab_game_sound_init(void)
{
    initialized = rt_mutex_init(&lock, "game_sfx", RT_IPC_FLAG_PRIO) == RT_EOK;
    if (!initialized) rt_kprintf("[lab:game:sound] Audio lock unavailable.\n");
}
unsigned lab_game_sound_volume(void)
{
    if (!initialized) return 6;
    rt_mutex_take(&lock,RT_WAITING_FOREVER);
    unsigned volume=game_volume;
    rt_mutex_release(&lock);
    return volume;
}
/* 保存游戏独立音量，并使正在播放或等待的旧音效失效。 */
void lab_game_sound_set_volume(unsigned volume)
{
    if (!initialized || volume>15) return;
    rt_mutex_take(&lock,RT_WAITING_FOREVER);
    if (game_volume!=volume) {
        game_volume=volume; generation++; pending=GAME_SOUND_NONE;
    }
    rt_mutex_release(&lock);
}
/* 通过会话编号判断当前播放是否仍有效，供送音和收尾等待及时取消。 */
static bool current(unsigned token)
{
    rt_mutex_take(&lock, RT_WAITING_FOREVER);
    bool yes=active && generation==token;
    rt_mutex_release(&lock);
    return yes;
}
/* 取消当前音效并清空待播消息，不在调用线程等待设备关闭。 */
void lab_game_sound_quiet(void)
{
    if (!initialized) return;
    rt_mutex_take(&lock, RT_WAITING_FOREVER);
    generation++; pending=GAME_SOUND_NONE;
    rt_mutex_release(&lock);
}
/* 向单槽消息提交提示音，较高枚举值优先，避免音效越积越多。 */
void lab_game_sound_emit(game_sound_t cue)
{
    if (!initialized || cue<=GAME_SOUND_NONE || cue>GAME_SOUND_LOSE) return;
    rt_mutex_take(&lock, RT_WAITING_FOREVER);
    if (active && available && cue>=pending) pending=cue;
    rt_mutex_release(&lock);
}
/* PCM lives in flash. Never synthesize trigonometric samples in the
 * real-time feeder: game rendering can otherwise starve the small DMA ring. */
static unsigned sound_samples(game_sound_t cue)
{
    return game_pcm[cue].count;
}
/* 从 Flash 复制 PCM 到可写小块，处理部分写入、播放收尾和音量恢复。 */
static int play(game_sound_t cue, unsigned token)
{
    unsigned volume=lab_game_sound_volume();
    if (!volume || !current(token)) return 0;
    uint8_t old=audio_server_get_private_volume(AUDIO_TYPE_LOCAL_MUSIC);
    if (audio_server_set_private_volume(AUDIO_TYPE_LOCAL_MUSIC,volume)!=0) {
        audio_server_set_private_volume(AUDIO_TYPE_LOCAL_MUSIC,old); return -1;
    }
    audio_parameter_t p={0};
    p.write_samplerate=SOUND_SAMPLE_RATE; p.write_channnel_num=1;
    p.write_bits_per_sample=16; p.write_cache_size=1024;
    audio_client_t client=audio_open2(AUDIO_TYPE_LOCAL_MUSIC,AUDIO_TX,&p,NULL,NULL,AUDIO_DEVICE_SPEAKER);
    int error=client ? 0 : -1;
    unsigned offset=0, total=sound_samples(cue);
    rt_tick_t started=rt_tick_get(), progress=started;
    rt_tick_t last_write=started, max_gap=0;
    int16_t block[SOUND_BLOCK_SAMPLES];
    while (client && offset<total && current(token)) {
        unsigned n=total-offset; if (n>SOUND_BLOCK_SAMPLES) n=SOUND_BLOCK_SAMPLES;
        /* SDK may apply volume/fade in place; do not pass flash directly. */
        memcpy(block,game_pcm[cue].data+offset,n*sizeof(block[0]));
        int wrote=audio_write(client,(uint8_t *)block,n*2);
        if (wrote<0 || wrote>(int)n*2 || (wrote&1)) { error=-1; break; }
        if (wrote) {
            rt_tick_t now=rt_tick_get(), gap=now-last_write;
            if (gap>max_gap) max_gap=gap;
            last_write=now; offset+=wrote/2; progress=now;
        }
        else if ((rt_tick_t)(rt_tick_get()-progress)>rt_tick_from_millisecond(300)) { error=-1; break; }
        rt_thread_mdelay(wrote ? 1 : 5);
    }
    rt_tick_t feed_ticks=rt_tick_get()-started;
    if (client && !error && current(token)) {
        uint32_t drain=0;
        if (audio_ioctl(client,AUDIO_IOCTL_FLUSH_TIME_MS,&drain)!=0 || drain>500) error=-1;
        else {
            /* FLUSH_TIME_MS only measures the client ring, not downstream
             * mixer/DMA data. Enqueuing PCM is faster than hearing it: keep
             * the output alive for at least the complete sample timeline,
             * then allow downstream buffers to finish. All waits cancelable.
             * SDK v2.5.1 speaker mixer has 16 DMA blocks (160 ms at 16 kHz);
             * 200 ms also covers the non-mixed DMA path used by this board. */
            rt_tick_t duration=rt_tick_from_millisecond((total+SOUND_SAMPLES_PER_MS-1)/SOUND_SAMPLES_PER_MS);
            rt_tick_t elapsed=rt_tick_get()-started;
            rt_tick_t wait=rt_tick_from_millisecond(drain);
            if (elapsed<duration && duration-elapsed>wait) wait=duration-elapsed;
            wait+=rt_tick_from_millisecond(SOUND_DRAIN_MARGIN_MS);
            rt_tick_t draining=rt_tick_get();
            while ((rt_tick_t)(rt_tick_get()-draining)<wait && current(token)) rt_thread_mdelay(10);
        }
    }
    bool cancelled=!current(token);
    if (client && audio_close(client)!=0) error=-1;
    if (audio_server_set_private_volume(AUDIO_TYPE_LOCAL_MUSIC,old)!=0) error=-1;
    static const char *names[]={"none","fish","heal","hit","start","win","lose"};
    rt_kprintf("[lab:game:sound] %s pcm=%u/%u ms feed_ticks=%lu max_gap_ticks=%lu elapsed_ticks=%lu tick_hz=%u %s\n",
               names[cue],offset/SOUND_SAMPLES_PER_MS,total/SOUND_SAMPLES_PER_MS,(unsigned long)feed_ticks,(unsigned long)max_gap,
               (unsigned long)(rt_tick_get()-started),
               (unsigned)rt_tick_from_millisecond(1000),error ? "error" : cancelled ? "cancelled" : "done");
    return error;
}
/* 顺序消费待播音效；音频失败时静音本次会话，不改写游戏结果。 */
static void worker(void *arg)
{
    (void)arg;
    while (1) {
        rt_mutex_take(&lock,RT_WAITING_FOREVER);
        if (!active) { running=false; rt_mutex_release(&lock); return; }
        game_sound_t cue=pending; pending=GAME_SOUND_NONE;
        unsigned token=generation;
        rt_mutex_release(&lock);
        if (cue && play(cue,token)) {
            rt_mutex_take(&lock,RT_WAITING_FOREVER);
            available=false; pending=GAME_SOUND_NONE;
            rt_mutex_release(&lock);
            rt_kprintf("[lab:game:sound] Audio failed; muted until next game session.\n");
        }
        rt_thread_mdelay(10);
    }
}
/* 为本次游戏启动音效任务，并安排出发提示音。 */
void lab_game_sound_begin(void)
{
    if (!initialized) return;
    rt_mutex_take(&lock,RT_WAITING_FOREVER);
    if (running) { rt_mutex_release(&lock); return; }
    active=running=available=true; generation++; pending=GAME_SOUND_START;
    rt_mutex_release(&lock);
    /* Main/LVGL runs at 19. Brief PCM copies at 18 must meet audio deadlines;
     * every iteration yields, and the audio server/BT retain higher priority. */
    rt_thread_t thread=rt_thread_create("game_sfx",worker,NULL,4096,SOUND_THREAD_PRIORITY,10);
    if (thread && rt_thread_startup(thread)==RT_EOK) return;
    if (thread) rt_thread_delete(thread);
    rt_mutex_take(&lock,RT_WAITING_FOREVER);
    active=running=available=false; pending=GAME_SOUND_NONE;
    rt_mutex_release(&lock);
    rt_kprintf("[lab:game:sound] Audio task unavailable.\n");
}
/* 使本会话失效，并由 lab_io 等待音频客户端关闭后再释放外设占用。 */
void lab_game_sound_end(void)
{
    if (!initialized) return;
    rt_mutex_take(&lock,RT_WAITING_FOREVER);
    active=false; pending=GAME_SOUND_NONE; generation++;
    rt_mutex_release(&lock);
    /* Only the peripheral worker waits, never the UI or Bluetooth threads. */
    while (1) {
        rt_mutex_take(&lock,RT_WAITING_FOREVER);
        bool wait=running;
        rt_mutex_release(&lock);
        if (!wait) return;
        rt_thread_mdelay(10);
    }
}
