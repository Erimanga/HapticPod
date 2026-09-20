#include "lab_ride.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
static void step(lab_ride_t *r, lab_motion_t *s, unsigned n) { while (n--) lab_ride_step(r, s, 0.033f); }
static void start(lab_ride_t *r, lab_motion_t *s)
{
    *s=(lab_motion_t){.a={0,0,1}, .valid=true}; lab_ride_reset(r);
    step(r,s,1); assert(r->phase==RIDE_PLAYING && r->time==60);
}
int main(void)
{
    lab_ride_t r = {0}; lab_motion_t s;
    start(&r,&s); step(&r,&s,50); assert(r.distance==0 && r.speed==0);
    /* Raw-axis signs inferred from the reported movement, not measured I2C logs.
       Enter flat, then bottom/buttons down => down; right edge raised => left. */
    start(&r,&s); s.a[0]=1; s.a[1]=0; s.a[2]=0; step(&r,&s,20);
    assert(r.distance < 0 && r.speed < 0 && fabsf(r.x)<0.001f);
    start(&r,&s); s.a[0]=0; s.a[1]=1; s.a[2]=0; step(&r,&s,20);
    assert(r.x < 0 && r.heading < 0 && fabsf(r.distance)<0.001f);
    start(&r,&s); s.a[0]=-1; s.a[1]=0; s.a[2]=0; step(&r,&s,20);
    assert(r.distance > 0 && r.speed > 0 && fabsf(r.x)<0.001f);
    start(&r,&s); s.a[0]=0; s.a[1]=-1; s.a[2]=0; step(&r,&s,20);
    assert(r.x > 0 && r.heading > 0 && fabsf(r.distance)<0.001f);
    start(&r,&s);
    /* Both screen-Y directions must move the avatar, even backwards at the start. */
    s.a[0]=-0.35f; s.a[2]=0.94f; step(&r,&s,20);
    assert(r.distance>0.5f && r.distance-r.camera>0.5f && r.x==0);
    step(&r,&s,80); assert(r.distance>10 && r.speed>4 && r.distance-r.camera<=10.001f);
    s.a[0]=0.35f; step(&r,&s,100); assert(r.speed < -2 && r.distance-r.camera < -2);
    start(&r,&s); s.a[0]=0.35f; s.a[2]=0.94f; step(&r,&s,70);
    assert(r.distance<0 && r.speed < -2 && r.distance-r.camera >= -6.001f);
    /* Screen-X tilt turns at rest; mirrored inputs produce mirrored steering. */
    start(&r,&s); s.a[1]=-0.22f; s.a[2]=0.975f; step(&r,&s,20);
    assert(r.x>0 && r.heading>15 && r.distance==0);
    float right=r.x, heading=r.heading;
    start(&r,&s); s.a[1]=0.22f; s.a[2]=0.975f; step(&r,&s,20);
    assert(fabsf(r.x+right)<0.001f && fabsf(r.heading+heading)<0.001f);
    /* Diagonal movement and heading affect the real trajectory. */
    start(&r,&s); s.a[1]=-0.17f; s.a[0]=-0.25f; s.a[2]=0.953f; step(&r,&s,25);
    assert(r.x>0.05f && r.distance>0.5f && r.heading>10);
    float d=r.distance, x=r.x; r.phase=RIDE_PAUSED; step(&r,&s,100); assert(r.distance==d && r.x==x);
    lab_ride_calibrate(&r); step(&r,&s,1); step(&r,&s,20);
    assert(r.phase==RIDE_PLAYING && r.speed==0 && r.distance==d && r.x==x);
    s.valid=false; step(&r,&s,100); assert(r.distance==d);
    /* Invalid gravity cannot initialize the center. No stable-percent gate. */
    lab_ride_reset(&r); s=(lab_motion_t){.valid=true}; step(&r,&s,20); assert(r.phase==RIDE_CALIBRATING);
    s.a[2]=1; s.g[0]=20; step(&r,&s,1); assert(r.phase==RIDE_PLAYING);
    /* Swept crossing and reverse recross cannot duplicate score. */
    start(&r,&s); r.pitch=20;
    s.a[0]=-0.342f; s.a[2]=0.94f;
    r.x=lab_ride_lane(0); r.distance=9.95f; r.speed=8;
    step(&r,&s,1); assert(r.fish==1);
    r.distance=10.05f; r.speed=-8; step(&r,&s,1); assert(r.fish==1);
    r.x=1.1f; r.immunity=0; unsigned lives=r.lives; step(&r,&s,1); assert(r.lives==lives-1);
    r.x=1.1f; step(&r,&s,1); assert(r.lives==lives-1);
    r.time=0.01f; step(&r,&s,1); assert(r.phase==RIDE_OVER);
    lab_ride_reset(&r); assert(r.time==60 && r.lives==3 && r.heading==0);
    assert(10+(RIDE_ROUTE_ITEMS-1)*12 > 60*11.25f); /* Route covers a full Hard run. */
    /* Rare supplies heal once in either direction and cannot exceed the cap. */
    start(&r,&s); r.lives=2; r.distance=33.95f; r.speed=8; r.pitch=20;
    s.a[0]=-0.342f; s.a[2]=0.94f; step(&r,&s,1);
    assert(r.lives==3 && r.supplies[0] && r.event==RIDE_EVENT_HEAL);
    r.distance=34.05f; r.speed=-8; step(&r,&s,1); assert(r.lives==3);
    start(&r,&s); r.lives=RIDE_MAX_HP; r.distance=34.05f; r.speed=-8;
    step(&r,&s,1); assert(r.lives==RIDE_MAX_HP && r.event==RIDE_EVENT_FULL && r.supplies[0]);
    start(&r,&s); r.x=0.62f; r.distance=33.95f; r.speed=8;
    step(&r,&s,1); assert(!r.supplies[0] && r.lives==3);
    start(&r,&s); r.lives=1; r.immunity=0; r.x=1.1f; r.distance=33.95f; r.speed=8;
    step(&r,&s,1); assert(r.phase==RIDE_OVER && r.lives==0 && !r.supplies[0]);
    /* Settings change in-place and survive reset; Easy is slower/more forgiving. */
    r.phase=RIDE_PAUSED; r.time=25; r.fish=7; r.lives=4;
    lab_ride_difficulty(&r,RIDE_HARD); assert(r.time==25 && r.fish==7 && r.lives==4 && r.phase==RIDE_PAUSED);
    lab_ride_difficulty(&r,(ride_difficulty_t)99); assert(r.difficulty==RIDE_HARD);
    lab_ride_reset(&r); assert(r.difficulty==RIDE_HARD && r.lives==3 && !r.supplies[0]);
    start(&r,&s); s.a[0]=-0.35f; s.a[2]=0.94f; step(&r,&s,20); float hard_speed=r.speed;
    lab_ride_difficulty(&r,RIDE_EASY); start(&r,&s); s.a[0]=-0.35f; s.a[2]=0.94f; step(&r,&s,20);
    assert(hard_speed > r.speed*1.5f);
    start(&r,&s); r.immunity=0; r.roll=46; s.a[1]=-0.719f; s.a[2]=0.695f; step(&r,&s,1); assert(r.lives==3);
    lab_ride_difficulty(&r,RIDE_HARD); step(&r,&s,1); assert(r.lives==2);
    puts("PASS: HP pickup, reverse dedup, cap, lane miss, fatal-hit protection and difficulty persistence/speed/tolerance.");
    puts("PASS: instant center, both X/Y directions, reverse from start, camera limits, heading/path, pause/recenter, invalid input, pickup, immunity and finish.");
}
