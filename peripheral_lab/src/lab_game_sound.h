/*
 * lab_game_sound.h — 游戏音效接口：UI 提交短音提示，外设工作线程负责会话启动和结束清理。
 */
#ifndef LAB_GAME_SOUND_H
#define LAB_GAME_SOUND_H
#include <stdbool.h>
typedef enum { GAME_SOUND_NONE, GAME_SOUND_FISH, GAME_SOUND_HEAL, GAME_SOUND_HIT,
               GAME_SOUND_START, GAME_SOUND_WIN, GAME_SOUND_LOSE } game_sound_t;
/* 初始化共享锁；音效线程在进入游戏时创建。 */
void lab_game_sound_init(void);
/* 游戏独立音量，0 为静音；调整时取消旧音效。 */
unsigned lab_game_sound_volume(void);
void lab_game_sound_set_volume(unsigned volume);
/* begin/end run in lab_io, commands/quiet may run in LVGL. */
void lab_game_sound_begin(void);
/* 结束会话并等待音频关闭，只能从允许等待的外设工作线程调用。 */
void lab_game_sound_end(void);
/* 只提交待播提示，不等待播放完成，允许界面调用。 */
void lab_game_sound_emit(game_sound_t cue);
/* 取消当前与待播音效，不阻塞界面等待音频收尾。 */
void lab_game_sound_quiet(void);
#endif
