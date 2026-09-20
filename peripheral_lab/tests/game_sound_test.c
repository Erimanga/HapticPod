#include "lab.h"
#include "lab_game_sound.h"
#include "audio_server.h"
#include <assert.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <time.h>
/* Include the production service to exercise playback and mailbox internals. */
#include "../src/lab_game_sound.c"
static atomic_uint ticks, opens;
static unsigned volume=9, bytes, closes, writes, peak;
static bool stall, reject_open, partial, cancel_audio, bad_write, delayed_sink;
static unsigned sink_end;
static bool dma_sink;
static unsigned queued_bytes, dma_at, feed_underruns;
static const int16_t *expected_pcm;
static int16_t sound_sample(game_sound_t cue,unsigned pos) { return game_pcm[cue].data[pos]; }
rt_tick_t rt_tick_get(void) { return atomic_load(&ticks); }
rt_tick_t rt_tick_from_millisecond(unsigned ms) { return ms; }
void rt_thread_mdelay(unsigned ms)
{
    unsigned t=atomic_fetch_add(&ticks,ms)+ms;
    if (dma_sink) {
        while (t>=dma_at) {
            if (queued_bytes>=960) queued_bytes-=960;
            else if (bytes && bytes<game_pcm[GAME_SOUND_WIN].count*2) feed_underruns++;
            dma_at+=30;
        }
    }
    if (cancel_audio && t>=30) lab_game_sound_quiet();
    struct timespec ts={0,100000}; nanosleep(&ts,NULL);
}
int rt_mutex_init(struct rt_mutex *m,const char *n,unsigned f) { (void)n;(void)f;return pthread_mutex_init(&m->mutex,NULL); }
int rt_mutex_take(struct rt_mutex *m,int t) { (void)t;return pthread_mutex_lock(&m->mutex); }
int rt_mutex_release(struct rt_mutex *m) { return pthread_mutex_unlock(&m->mutex); }
struct host_thread { void (*fn)(void *); void *arg; };
static void *run(void *p) { struct host_thread *t=p; t->fn(t->arg); free(t); return NULL; }
rt_thread_t rt_thread_create(const char *n,void (*fn)(void *),void *a,unsigned st,unsigned pr,unsigned sl)
{ (void)n;(void)st;(void)pr;(void)sl;struct host_thread *t=malloc(sizeof(*t)); assert(t);t->fn=fn;t->arg=a;return t; }
int rt_thread_startup(rt_thread_t t) { pthread_t p;int e=pthread_create(&p,NULL,run,t);if (!e) pthread_detach(p);return e; }
int rt_thread_delete(rt_thread_t t) { free(t);return 0; }
uint8_t audio_server_get_private_volume(audio_type_t t) { (void)t;return volume; }
int audio_server_set_private_volume(audio_type_t t,uint8_t v) { (void)t;volume=v;return 0; }
audio_client_t audio_open2(audio_type_t t,audio_rwflag_t rw,audio_parameter_t *p,audio_server_callback_func cb,void *u,audio_device_e d)
{
    (void)t;(void)rw;(void)cb;(void)u;(void)d;
    assert(p->write_samplerate==16000 && p->write_cache_size==1024 && p->write_bits_per_sample==16 && p->write_channnel_num==1);
    atomic_fetch_add(&opens,1);return reject_open ? NULL : (void *)1;
}
int audio_write(audio_client_t c,uint8_t *b,uint32_t n)
{
    assert(c && n<=320); writes++;
    if (bad_write) return 1;
    if (stall) return 0;
    if (dma_sink && queued_bytes+n>1924) return 0;
    unsigned accepted=partial && n>4 ? (n/2)&~1u : n;
    for (unsigned i=0;i<accepted/2;++i) { unsigned a=abs(((int16_t *)b)[i]); if (a>peak) peak=a; }
    if (delayed_sink) {
        /* The client ring empties immediately into a downstream queue;
         * ioctl cannot see its 160 ms latency or unplayed PCM. */
        unsigned earliest=atomic_load(&ticks)*16;
        if (!bytes) earliest+=160*16;
        if (sink_end<earliest) sink_end=earliest;
        sink_end+=accepted/2;
    }
    if (expected_pcm) {
        assert(!memcmp(b,expected_pcm+bytes/2,accepted));
        /* SDK is permitted to modify caller buffers. Flash must stay intact. */
        memset(b,0,accepted);
    }
    if (dma_sink) queued_bytes+=accepted;
    bytes+=accepted;return accepted;
}
int audio_ioctl(audio_client_t c,int cmd,void *p) { assert(c && cmd==AUDIO_IOCTL_FLUSH_TIME_MS);*(uint32_t *)p=delayed_sink ? 0 : 20;return 0; }
int audio_close(audio_client_t c) { assert(c);if (delayed_sink && !cancel_audio) assert(atomic_load(&ticks)*16>=sink_end);closes++;return 0; }
static void reset(void)
{
    ticks=opens=0; bytes=closes=writes=peak=0; volume=9;lab_game_sound_set_volume(6);
    stall=reject_open=partial=cancel_audio=bad_write=delayed_sink=false; sink_end=0; expected_pcm=NULL; dma_sink=false; queued_bytes=feed_underruns=0; dma_at=30;
    active=available=true;running=false;pending=GAME_SOUND_NONE;generation++;
}
static void check_score(game_sound_t cue, const char *name)
{
    unsigned samples=sound_samples(cue), energy=0;
    int previous=0, max_jump=0;
    FILE *file=fopen(name,"wb"); assert(file);
    for (unsigned i=0;i<samples;++i) {
        int16_t sample=sound_sample(cue,i);
        if (i<SOUND_HEAD || i>=samples-SOUND_TAIL) assert(sample==0);
        assert(abs(sample)<=3000);
        if (abs(sample-previous)>max_jump) max_jump=abs(sample-previous);
        energy+=abs(sample); previous=sample;
        fwrite(&sample,sizeof(sample),1,file);
    }
    fclose(file); assert(energy>samples*200 && max_jump<2200 && previous==0);
    unsigned boundary=SOUND_HEAD;
    static const unsigned durations[][4]={
        {0}, {80,80}, {80,80,80}, {150}, {80,80}, {120,120,140,300}, {150,150,300}
    };
    for (unsigned i=0;i<4 && durations[cue][i];++i) {
        assert(sound_sample(cue,boundary)==0);
        boundary+=durations[cue][i]*16;
        assert(sound_sample(cue,boundary-161)==0); /* envelope endpoint */
        assert(sound_sample(cue,boundary-1)==0);
    }
}
int main(void)
{
    check_score(GAME_SOUND_HIT,"game-hit.pcm");
    check_score(GAME_SOUND_WIN,"game-win.pcm");
    check_score(GAME_SOUND_LOSE,"game-lose.pcm");
    assert(sound_samples(GAME_SOUND_WIN)>10000 && sound_samples(GAME_SOUND_LOSE)>10000);
    lab_game_sound_init(); assert(initialized);
    reset();partial=true;expected_pcm=game_pcm[GAME_SOUND_HEAL].data;assert(!play(GAME_SOUND_HEAL,generation));
    assert(bytes==sound_samples(GAME_SOUND_HEAL)*2 && peak>2000 && peak<=3000 && volume==9 && closes==1);
    for (game_sound_t cue=GAME_SOUND_FISH;cue<=GAME_SOUND_LOSE;cue++) {
        reset();delayed_sink=true;expected_pcm=game_pcm[cue].data;assert(!play(cue,generation));
        assert(bytes==sound_samples(cue)*2 && closes==1 && volume==9);
    }
    /* SDK non-mixed path consumes 960 bytes every 30 ms at 16 kHz mono,
     * with a minimum 1924-byte ring. Exercise real backpressure and cadence. */
    reset();dma_sink=true;expected_pcm=game_pcm[GAME_SOUND_WIN].data;
    assert(!play(GAME_SOUND_WIN,generation));
    assert(!feed_underruns && bytes==sound_samples(GAME_SOUND_WIN)*2 && ticks<1100);
    reset();delayed_sink=cancel_audio=true;assert(!play(GAME_SOUND_WIN,generation));
    assert(ticks<100 && closes==1 && volume==9);
    reset();stall=true;assert(play(GAME_SOUND_FISH,generation));assert(ticks<450 && closes==1 && volume==9);
    reset();cancel_audio=true;assert(!play(GAME_SOUND_WIN,generation));assert(bytes<sound_samples(GAME_SOUND_WIN)*2 && closes==1 && volume==9);
    reset();reject_open=true;assert(play(GAME_SOUND_START,generation));assert(!closes && volume==9);
    reset();bad_write=true;assert(play(GAME_SOUND_HIT,generation));assert(closes==1 && volume==9);
    reset();lab_game_sound_set_volume(0);assert(!play(GAME_SOUND_FISH,generation));assert(!opens && volume==9);
    reset();lab_game_sound_emit(GAME_SOUND_FISH);lab_game_sound_emit(GAME_SOUND_LOSE);lab_game_sound_emit(GAME_SOUND_FISH);
    assert(pending==GAME_SOUND_LOSE);unsigned old=generation;lab_game_sound_quiet();assert(!pending && !current(old));
    lab_game_sound_end();lab_game_sound_emit(GAME_SOUND_HEAL);assert(!pending);
    lab_game_sound_set_volume(12);assert(lab_game_sound_volume()==12);
    lab_game_sound_set_volume(16);assert(lab_game_sound_volume()==12);
    lab_game_sound_emit(GAME_SOUND_HEAL);unsigned vtoken=generation;
    lab_game_sound_set_volume(0);assert(!pending && !current(vtoken));
    /* Real POSIX thread exercises start/cancel/join handshake and restart. */
    reset();active=false;lab_game_sound_begin();
    for (int i=0;i<1000 && !atomic_load(&opens);++i) { struct timespec t={0,100000};nanosleep(&t,NULL); }
    assert(atomic_load(&opens));lab_game_sound_end();assert(!running && !active && closes==1 && volume==9);
    unsigned count=opens;lab_game_sound_begin();
    for (int i=0;i<1000 && atomic_load(&opens)==count;++i) { struct timespec t={0,100000};nanosleep(&t,NULL); }
    lab_game_sound_end();assert(opens>count && volume==9);
    puts("PASS: game sound PCM, delayed downstream drain, partial writes, cancellation, stall, open/write errors, mute, priority, stale cues and threaded stop/restart.");
}
