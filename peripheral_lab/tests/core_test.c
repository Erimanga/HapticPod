/* Run the production job/state core with POSIX synchronization and fake I/O. */
#include "lab.h"
#include <assert.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <time.h>

struct host_thread { void (*entry)(void *); void *arg; };
int rt_mutex_init(struct rt_mutex *m, const char *n, unsigned f) { (void)n; (void)f; return pthread_mutex_init(&m->mutex, NULL); }
int rt_mutex_take(struct rt_mutex *m, int t) { (void)t; return pthread_mutex_lock(&m->mutex); }
int rt_mutex_release(struct rt_mutex *m) { return pthread_mutex_unlock(&m->mutex); }
int rt_sem_init(struct rt_semaphore *s, const char *n, unsigned c, unsigned f)
{
    (void)n; (void)f; s->count = c;
    pthread_mutex_init(&s->mutex, NULL); return pthread_cond_init(&s->cond, NULL);
}
int rt_sem_take(struct rt_semaphore *s, int t)
{
    (void)t;
    pthread_mutex_lock(&s->mutex);
    while (!s->count) pthread_cond_wait(&s->cond, &s->mutex);
    s->count--; pthread_mutex_unlock(&s->mutex); return 0;
}
int rt_sem_release(struct rt_semaphore *s)
{
    pthread_mutex_lock(&s->mutex); s->count++; pthread_cond_signal(&s->cond);
    pthread_mutex_unlock(&s->mutex); return 0;
}
static void *entry(void *p) { struct host_thread *t = p; t->entry(t->arg); return NULL; }
rt_thread_t rt_thread_create(const char *n, void (*fn)(void *), void *arg, unsigned stack, unsigned priority, unsigned slice)
{
    (void)n; (void)stack; (void)priority; (void)slice;
    struct host_thread *t = malloc(sizeof(*t)); assert(t); t->entry = fn; t->arg = arg; return t;
}
int rt_thread_startup(rt_thread_t t) { pthread_t thread; int e = pthread_create(&thread, NULL, entry, t); if (!e) pthread_detach(thread); return e; }
void rt_thread_mdelay(unsigned ms) { struct timespec t = {ms / 1000, (ms % 1000) * 1000000}; nanosleep(&t, NULL); }

static atomic_bool fail_sample;
static atomic_uint calls;
static void sample(unsigned action)
{
    (void)action;
    atomic_fetch_add(&calls, 1);
    rt_thread_mdelay(20);
    lab_publish(LAB_ADC, atomic_load(&fail_sample) ? LAB_FAIL : LAB_PASS, "3.8000 V", "Fake ADC acquisition");
}
static void note(unsigned action) { assert(action == 7); lab_publish(LAB_SPEAKER, LAB_PASS, "C5", "Eighth action"); }
static void once(unsigned action) { (void)action; lab_publish(LAB_RTC, LAB_PASS, "Tick", "Fake RTC"); }
const lab_module_t lab_modules[LAB_COUNT] = {
    [LAB_ADC] = {.title = "ADC", .actions = {"Read", "Continuous"}, .run = sample},
    [LAB_SPEAKER] = {.title = "Speaker", .actions = {[7] = "C5"}, .run = note},
    [LAB_RTC] = {.title = "RTC", .actions = {"Read"}, .run = once}
};
static void wait_idle(void)
{
    for (int i = 0; i < 300 && lab_busy(); ++i) rt_thread_mdelay(10);
    assert(!lab_busy());
}
int main(void)
{
    assert(lab_init() == 0);
    assert(!lab_start((lab_id_t)-1, 0));
    assert(!lab_start(LAB_SPEAKER, LAB_ACTION_COUNT));
    unsigned v, f;
    lab_speaker_configure(15, 4000); lab_speaker_settings(&v, &f);
    assert(v == 15 && f == 4000);
    lab_speaker_configure(16, 5000); lab_speaker_settings(&v, &f);
    assert(v == 15 && f == 4000);
    assert(lab_start(LAB_SPEAKER, 7)); wait_idle();
    assert(lab_start(LAB_ADC, 1));
    assert(!lab_start(LAB_RTC, 0));
    lab_result_t r;
    for (int i = 0; i < 300; ++i) {
        lab_snapshot(LAB_ADC, &r);
        if (r.samples >= 3) break;
        rt_thread_mdelay(10);
    }
    assert(r.samples >= 3 && r.status == LAB_RUNNING && lab_continuous());
    lab_stop(); wait_idle();
    lab_snapshot(LAB_ADC, &r);
    assert(r.status == LAB_PASS && !lab_continuous());
    unsigned count = atomic_load(&calls);
    rt_thread_mdelay(100);
    assert(atomic_load(&calls) == count);
    assert(lab_start(LAB_RTC, 0)); wait_idle();
    lab_snapshot(LAB_RTC, &r); assert(r.status == LAB_PASS);
    atomic_store(&fail_sample, true);
    assert(lab_start(LAB_ADC, 1)); wait_idle();
    lab_snapshot(LAB_ADC, &r);
    assert(r.status == LAB_FAIL && r.samples == 1 && !lab_continuous());
    puts("PASS: production worker continuous sampling, busy exclusion, stop, restart and failure exit.");
    return 0;
}

void lab_game_sound_init(void) {}
