/* Exercise the actual worker with register I/O faults and a cancellable clock. */
#include "lab.h"
#include "lab_game.h"
#include <assert.h>
#include <string.h>
static struct rt_i2c_bus_device bus;
static uint8_t regs[256];
static unsigned elapsed, samples, restore_mask;
static int fail_reg, fail_write;
static bool stop, no_ready;
static lab_status_t status;
struct rt_i2c_bus_device *lab_i2c(const char *n) { assert(!strcmp(n,"i2c3")); return &bus; }
rt_err_t lab_reg_read(struct rt_i2c_bus_device *b, uint8_t a, uint8_t r, void *p, uint16_t n)
{
    assert(b==&bus && a==0x6A);
    if (r==fail_reg) return -1;
    if (r==0x1E) { *(uint8_t *)p=no_ready ? 0 : 3; return 0; }
    memcpy(p,regs+r,n); return 0;
}
rt_err_t lab_reg_write(struct rt_i2c_bus_device *b, uint8_t a, uint8_t r, uint8_t v)
{
    assert(b==&bus && a==0x6A);
    if (v==r) restore_mask |= 1u << (r-0x10);
    if (r==fail_write) { fail_write=-1; return -1; }
    regs[r]=v; return 0;
}
bool lab_cancelled(void) { return stop; }
void rt_thread_mdelay(unsigned ms) { elapsed+=ms; if (!no_ready && elapsed>=60) stop=true; }
void lab_motion_publish(const lab_motion_t *s)
{
    if (s->valid) { samples++; assert(s->sequence==samples); assert(s->a[2] > 0.99f && s->a[2]<1.01f); }
}
void lab_publish(lab_id_t id, lab_status_t st, const char *v, const char *d) { (void)v; (void)d; assert(id==LAB_GAME); status=st; }
void lab_error(lab_id_t id, const char *s, int e) { (void)s; assert(id==LAB_GAME && e); status=LAB_FAIL; }
static void reset(void)
{
    memset(regs,0,sizeof(regs)); regs[0x0F]=0x6A;
    regs[0x10]=0x10; regs[0x11]=0x11; regs[0x12]=0x12;
    regs[0x2D]=0x40; elapsed=samples=restore_mask=0;
    stop=no_ready=false; fail_reg=fail_write=-1; status=LAB_IDLE;
}
int main(void)
{
    reset(); lab_game_run(0); assert(samples==3 && restore_mask==7 && status==LAB_OBSERVE);
    assert(regs[0x10]==0x10 && regs[0x11]==0x11 && regs[0x12]==0x12);
    reset(); fail_reg=0x22; lab_game_run(0); assert(status==LAB_FAIL && restore_mask==7);
    reset(); fail_write=0x10; lab_game_run(0); assert(status==LAB_FAIL && restore_mask==7);
    reset(); no_ready=true; lab_game_run(0); assert(status==LAB_FAIL && elapsed<=540 && restore_mask==7);
    reset(); fail_reg=0x11; lab_game_run(0); assert(status==LAB_FAIL && restore_mask==0);
    reset(); stop=true; lab_game_run(0); assert(samples==0 && restore_mask==7);
    puts("PASS: game sensor cancellation, conversion, readiness timeout, config/data failure and restoration.");
}

void lab_game_sound_begin(void) {}
void lab_game_sound_end(void) {}
