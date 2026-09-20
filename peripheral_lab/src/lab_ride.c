/*
 * lab_ride.c — 鹈鹕自行车游戏模型：将倾斜输入转换为移动、转向、碰撞和得分，不操作硬件或 LVGL。
 */
#include "lab_ride.h"
#include <math.h>
#include <string.h>
static float clamp(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static float angle(float a) { while (a > 180) a -= 360; while (a < -180) a += 360; return a; }
static float dead(float a) { return fabsf(a) < 3 ? 0 : (a > 0 ? a - 3 : a + 3); }
float lab_ride_lane(unsigned i) { static const float lanes[] = {-0.62f, 0.0f, 0.62f, 0.0f}; return lanes[i % 4]; }
float lab_ride_supply_distance(unsigned i) { return 34 + i * 72; }
float lab_ride_supply_lane(unsigned i) { return lab_ride_lane(i + 1); }
/* 调整难度，不清空本局进度。 */
void lab_ride_difficulty(lab_ride_t *r, ride_difficulty_t difficulty)
{
    if ((unsigned)difficulty <= RIDE_HARD) r->difficulty = difficulty;
}
/* 进入居中阶段，下一份有效运动数据将作为新的中立握姿。 */
void lab_ride_calibrate(lab_ride_t *r)
{
    r->phase = RIDE_CALIBRATING;
    r->speed = r->heading = r->lean = 0;
}
/* 重置本局进度并保留难度，等待下一帧完成居中。 */
void lab_ride_reset(lab_ride_t *r)
{
    ride_difficulty_t difficulty = r->difficulty;
    memset(r, 0, sizeof(*r));
    lab_ride_difficulty(r, difficulty);
    r->lives = 3; r->time = 60; lab_ride_calibrate(r);
}
/* 在无敌时间外扣除生命并减速，生命耗尽则结束本局。 */
static void bump(lab_ride_t *r)
{
    if (r->immunity > 0) return;
    if (r->lives) r->lives--;
    r->speed *= 0.25f; r->lean = 0; r->immunity = r->difficulty == RIDE_EASY ? 2.5f : r->difficulty == RIDE_HARD ? 1.5f : 2; r->flash = 0.5f; r->event = RIDE_EVENT_HIT; r->event_revision++;
    if (!r->lives) r->phase = RIDE_OVER;
}
/* 按时间步处理倾斜、运动、碰撞和拾取；暂停或无效数据时不推进。 */
void lab_ride_step(lab_ride_t *r, const lab_motion_t *s, float dt)
{
    if (!s->valid || dt <= 0 || dt > 0.15f || r->phase == RIDE_PAUSED || r->phase == RIDE_OVER) return;
    /* sf32lb52-lchspi-ulp portrait mounting, buttons below the display.
       Board feedback: raw +X previously steered right when the bottom was down;
       raw +Y previously reversed when the right edge was raised.
       Screen frame is therefore (-sensor Y, +sensor X, sensor Z).
       Apply mounting before neutral-grip calibration, not to the game outputs. */
    float ax = -s->a[1], ay = s->a[0], az = s->a[2];
    float norm = sqrtf(ax * ax + ay * ay + az * az);
    if (!isfinite(norm) || norm < 0.2f || norm > 2.5f) return;
    float roll = atan2f(ax, az) * 57.29578f;
    float pitch = atan2f(-ay, sqrtf(ax * ax + az * az)) * 57.29578f;
    if (r->phase == RIDE_CALIBRATING) {
        r->zero_roll = roll; r->zero_pitch = pitch;
        r->roll = r->pitch = 0; r->immunity = 1.5f;
        r->phase = RIDE_PLAYING;
        return;
    }
    /* Low-pass gravity tilt; no yaw integration or stationary drift. */
    float alpha = dt / (0.12f + dt);
    r->roll += (angle(roll - r->zero_roll) - r->roll) * alpha;
    r->pitch += (pitch - r->zero_pitch - r->pitch) * alpha;
    float factor = r->difficulty == RIDE_EASY ? 0.75f : r->difficulty == RIDE_HARD ? 1.25f : 1;
    float steer = clamp(dead(r->roll) / (r->difficulty == RIDE_EASY ? 26 : r->difficulty == RIDE_HARD ? 18 : 22), -1.5f, 1.5f);
    float throttle = clamp(dead(r->pitch) / 20, -1, 1);
    r->speed += (throttle * (throttle < 0 ? 4 : 9) * factor - r->speed) * dt * 2.4f;
    float previous = r->distance;
    /* Heading affects the actual path as well as the drawing. At rest the
       rider can turn in place; forward/reverse movement follows that heading. */
    r->heading += (clamp(steer, -1, 1) * 55 - r->heading) * dt * 6;
    float radians = r->heading * 0.017453293f;
    r->distance += r->speed * cosf(radians) * dt;
    r->x += (r->speed * sinf(radians) * 0.16f + steer * 0.30f) * dt;
    r->lean += (steer * (0.18f + fabsf(r->speed) * 0.08f) - r->lean) * dt * 3;
    /* The bird moves on screen first. Camera follows only at the view edges.
       Reverse is allowed immediately, including behind the starting point. */
    float relative = r->distance - r->camera;
    float target = r->camera;
    if (relative > 6) target = r->distance - 6;
    else if (relative < -3) target = r->distance + 3;
    r->camera += (target - r->camera) * dt / (0.18f + dt);
    /* Keep the sprite within the full-screen view after a long frame. */
    r->camera = clamp(r->camera, r->distance - 10, r->distance + 6);
    r->time = fmaxf(0, r->time - dt); r->immunity = fmaxf(0, r->immunity - dt); r->flash = fmaxf(0, r->flash - dt);
    float tolerance = r->difficulty == RIDE_EASY ? 1.2f : r->difficulty == RIDE_HARD ? 0.85f : 1;
    if (fabsf(r->x) > 1 || fabsf(r->lean) > tolerance || fabsf(r->roll) > 42*tolerance || fabsf(r->pitch) > 48*tolerance) bump(r);
    if (r->phase == RIDE_OVER) return;
    r->x = clamp(r->x, -0.98f, 0.98f);
    for (unsigned i = 0; i < RIDE_ROUTE_ITEMS; ++i) {
        float pos = 10 + i * 12;
        /* Swept crossing works in both directions; each item scores only once. */
        if ((previous <= pos && r->distance >= pos) || (previous >= pos && r->distance <= pos)) {
            if (fabsf(r->x - lab_ride_lane(i)) < 0.27f && !r->collected[i]) { r->collected[i] = true; r->fish++; r->flash = 0.3f; r->event = RIDE_EVENT_FISH; r->event_revision++; }
        }
        pos += 6;
        if (((previous <= pos && r->distance >= pos) || (previous >= pos && r->distance <= pos)) &&
            fabsf(r->x + lab_ride_lane(i + 1)) < 0.25f && !r->hit[i]) { r->hit[i] = true; bump(r); }
    }
    if (r->phase == RIDE_OVER) return; /* Pickups cannot revive a fatal collision. */
    for (unsigned i=0; i<RIDE_SUPPLIES; ++i) {
        float pos=lab_ride_supply_distance(i);
        if (!r->supplies[i] && fabsf(r->x-lab_ride_supply_lane(i)) < 0.28f &&
            ((previous <= pos && r->distance >= pos) || (previous >= pos && r->distance <= pos))) {
            r->supplies[i]=true; r->event_revision++;
            r->event = r->lives < RIDE_MAX_HP ? RIDE_EVENT_HEAL : RIDE_EVENT_FULL;
            if (r->lives < RIDE_MAX_HP) r->lives++;
            r->flash=1.0f;
        }
    }
    if (r->time <= 0) r->phase = RIDE_OVER;
}
