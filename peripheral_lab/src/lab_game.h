/*
 * lab_game.h — 综合测试接口：用运动数据快照连接 IMU 工作线程与游戏界面。
 */
#ifndef LAB_GAME_H
#define LAB_GAME_H
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    float a[3], g[3]; /* g, degrees/sec */
    uint32_t sequence;
    bool valid;
} lab_motion_t;
/* 采样线程发布，界面线程读取；两者之间传递数据副本。 */
void lab_motion_publish(const lab_motion_t *sample);
void lab_motion_snapshot(lab_motion_t *sample);
/* 后台采样入口，退出时恢复 IMU 配置并结束音效会话。 */
void lab_game_run(unsigned action);
/* UI 入口，创建场景与菜单，并启动后台采样。 */
void lab_game_open(void);
#endif
