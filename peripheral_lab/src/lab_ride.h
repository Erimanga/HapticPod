/*
 * lab_ride.h — 游戏模型接口：定义本局状态、难度和事件，供界面按时间步推进。
 */
#ifndef LAB_RIDE_H
#define LAB_RIDE_H
#include "lab_game.h"
typedef enum { RIDE_CALIBRATING, RIDE_PLAYING, RIDE_PAUSED, RIDE_OVER } ride_phase_t;
#define RIDE_MAX_HP 5
#define RIDE_SUPPLIES 10
#define RIDE_ROUTE_ITEMS 64
typedef enum { RIDE_NORMAL, RIDE_EASY, RIDE_HARD } ride_difficulty_t;
typedef enum { RIDE_EVENT_NONE, RIDE_EVENT_FISH, RIDE_EVENT_HIT, RIDE_EVENT_HEAL, RIDE_EVENT_FULL } ride_event_t;
typedef struct {
    float x, distance, camera, speed, heading, lean, time, immunity, flash;
    float roll, pitch, zero_roll, zero_pitch;
    unsigned fish, lives, event_revision;
    bool collected[RIDE_ROUTE_ITEMS], hit[RIDE_ROUTE_ITEMS], supplies[RIDE_SUPPLIES];
    ride_difficulty_t difficulty;
    ride_event_t event;
    ride_phase_t phase;
} lab_ride_t;
/* 重开一局，保留难度并重新取中立握姿。 */
void lab_ride_reset(lab_ride_t *r);
/* 请求下一有效运动帧作为中立握姿。 */
void lab_ride_calibrate(lab_ride_t *r);
/* 根据运动快照和经过时间推进模型，不访问硬件或绘图接口。 */
void lab_ride_step(lab_ride_t *r, const lab_motion_t *s, float dt);
/* 道路物品和补给的位置规则，供模型判定与界面绘图共用。 */
float lab_ride_lane(unsigned index);
float lab_ride_supply_distance(unsigned index);
float lab_ride_supply_lane(unsigned index);
/* 更改难度，保留本局生命、得分和道具状态。 */
void lab_ride_difficulty(lab_ride_t *r, ride_difficulty_t difficulty);
#endif
