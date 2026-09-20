/*
 * lab.c — 测试调度与共享状态：lab_io 串行执行普通外设动作，UI 通过加锁快照读取结果。
 */
#include "lab.h"
#include "lab_game.h"
#include "lab_game_sound.h"
#include <string.h>
#include <stdio.h>

static struct rt_mutex state_lock;
static lab_motion_t motion;
/* 采样线程发布一份完整运动数据，避免界面读到更新一半的三轴值。 */
void lab_motion_publish(const lab_motion_t *sample)
{
    rt_mutex_take(&state_lock, RT_WAITING_FOREVER);
    motion = *sample;
    rt_mutex_release(&state_lock);
}
/* 复制最新运动数据，读取完成即释放锁，不把锁带入游戏计算。 */
void lab_motion_snapshot(lab_motion_t *sample)
{
    rt_mutex_take(&state_lock, RT_WAITING_FOREVER);
    *sample = motion;
    rt_mutex_release(&state_lock);
}
static struct rt_semaphore request_ready;
static lab_result_t results[LAB_COUNT];
static bool busy, continuous, stop_requested;
static lab_status_t sample_status;
static lab_touch_frame_t touch_frame;
static lab_id_t pending_id;
static unsigned pending_action;

/* 设置只在空闲时更新；工作线程读取同一份快照。 */
static unsigned speaker_volume = 6, speaker_frequency = 440;
/* 读取扬声器测试的音量和频率设置。 */
void lab_speaker_settings(unsigned *volume, unsigned *frequency)
{
    rt_mutex_take(&state_lock, RT_WAITING_FOREVER);
    *volume = speaker_volume; *frequency = speaker_frequency;
    rt_mutex_release(&state_lock);
}
/* 仅在外设空闲时接受有效设置，避免播放过程中改变测试条件。 */
void lab_speaker_configure(unsigned volume, unsigned frequency)
{
    rt_mutex_take(&state_lock, RT_WAITING_FOREVER);
    if (!busy && volume <= 15 && frequency >= 100 && frequency <= 4000) {
        speaker_volume = volume; speaker_frequency = frequency;
    }
    rt_mutex_release(&state_lock);
}

/* 更新测试结果及版本号；连续采样成功时仍显示运行中，直到停止。 */
void lab_publish(lab_id_t id, lab_status_t state, const char *value, const char *detail)
{
    rt_mutex_take(&state_lock, RT_WAITING_FOREVER);
    if (continuous && id == pending_id && busy) {
        sample_status = state;
        if (state == LAB_PASS) state = LAB_RUNNING;
    }
    results[id].status = state;
    rt_snprintf(results[id].value, sizeof(results[id].value), "%s", value);
    rt_snprintf(results[id].detail, sizeof(results[id].detail), "%s", detail);
    results[id].revision++;
    rt_mutex_release(&state_lock);
    rt_kprintf("[lab:%s] state=%d %s | %s\n", lab_modules[id].title, state, value, detail);
}

/* 在锁内复制结果，供 UI 在锁外刷新控件。 */
void lab_snapshot(lab_id_t id, lab_result_t *result)
{
    rt_mutex_take(&state_lock, RT_WAITING_FOREVER);
    *result = results[id];
    rt_mutex_release(&state_lock);
}

bool lab_busy(void)
{
    bool value;
    rt_mutex_take(&state_lock, RT_WAITING_FOREVER);
    value = busy;
    rt_mutex_release(&state_lock);
    return value;
}

bool lab_continuous(void)
{
    rt_mutex_take(&state_lock, RT_WAITING_FOREVER);
    bool value = busy && continuous;
    rt_mutex_release(&state_lock);
    return value;
}
/* 提交协作式停止请求；模块在安全位置检查并自行释放设备资源。 */
void lab_stop(void)
{
    rt_mutex_take(&state_lock, RT_WAITING_FOREVER);
    stop_requested = true;
    rt_mutex_release(&state_lock);
}
/* 供采样、接收和播放循环检查停止请求，不强制中断底层驱动。 */
bool lab_cancelled(void)
{
    rt_mutex_take(&state_lock, RT_WAITING_FOREVER);
    bool value = stop_requested;
    rt_mutex_release(&state_lock);
    return value;
}
/* 将后台解码的触点一次性发布给界面。 */
void lab_touch_frame_publish(const lab_touch_frame_t *frame)
{
    rt_mutex_take(&state_lock, RT_WAITING_FOREVER);
    touch_frame = *frame;
    rt_mutex_release(&state_lock);
}
/* 读取触点快照，供双指标记和导航手势判断使用。 */
void lab_touch_frame_snapshot(lab_touch_frame_t *frame)
{
    rt_mutex_take(&state_lock, RT_WAITING_FOREVER);
    *frame = touch_frame;
    rt_mutex_release(&state_lock);
}

/* 校验动作并占用唯一外设工作槽，再用信号量唤醒 lab_io。 */
bool lab_start(lab_id_t id, unsigned action)
{
    if ((unsigned)id >= LAB_COUNT || action >= LAB_ACTION_COUNT || !lab_modules[id].actions[action] ||
        !lab_modules[id].run) return false;
    rt_mutex_take(&state_lock, RT_WAITING_FOREVER);
    if (busy) { rt_mutex_release(&state_lock); return false; }
    busy = true;
    if (id == LAB_GAME) memset(&motion, 0, sizeof(motion));
    pending_id = id;
    pending_action = action;
    stop_requested = false;
    continuous = action == 1 && (id == LAB_ADC || id == LAB_IMU || id == LAB_MAG || id == LAB_LIGHT);
    sample_status = LAB_IDLE;
    results[id].samples = 0;
    rt_mutex_release(&state_lock);
    lab_publish(id, LAB_RUNNING, "Working...", "You can return to the dashboard while this test runs.");
    rt_sem_release(&request_ready);
    return true;
}

/* 将等待人工观察的结果转为通过或失败，运行中不接受确认。 */
void lab_confirm(lab_id_t id, bool pass)
{
    lab_result_t result;
    lab_snapshot(id, &result);
    if (!lab_busy() && result.status == LAB_OBSERVE)
        lab_publish(id, pass ? LAB_PASS : LAB_FAIL, pass ? "Confirmed" : "Needs attention",
                    pass ? "Visual / physical result confirmed by you." : "Command completed, but physical behavior was incorrect.");
}

/* 统一将设备操作错误转换为界面结果和串口诊断。 */
void lab_error(lab_id_t id, const char *operation, int error)
{
    char detail[160];
    rt_snprintf(detail, sizeof(detail), "%s failed (%d). Check wiring / power, then retry. Serial has details.", operation, error);
    lab_publish(id, LAB_FAIL, "Could not complete", detail);
}

/* 等待动作请求并串行执行；连续测试反复采样，停止或失败后释放 busy。 */
static void worker(void *parameter)
{
    (void)parameter;
    while (1) {
        rt_sem_take(&request_ready, RT_WAITING_FOREVER);
        bool repeat = lab_continuous();
        do {
            if (repeat) {
                rt_mutex_take(&state_lock, RT_WAITING_FOREVER);
                results[pending_id].samples++;
                rt_mutex_release(&state_lock);
            }
            lab_modules[pending_id].run(pending_action);
            lab_result_t result;
            lab_snapshot(pending_id, &result);
            if (!repeat || result.status == LAB_FAIL) break;
            /* 分段等待，使 Stop 在采样间隔内可及时响应。 */
            for (int i = 0; i < 25 && !lab_cancelled(); ++i) rt_thread_mdelay(20);
        } while (!lab_cancelled());
        if (repeat) {
            lab_result_t result;
            lab_snapshot(pending_id, &result);
            rt_mutex_take(&state_lock, RT_WAITING_FOREVER);
            continuous = false;
            lab_status_t final = sample_status;
            rt_mutex_release(&state_lock);
            lab_publish(pending_id, final, result.value, result.detail);
        }
        rt_mutex_take(&state_lock, RT_WAITING_FOREVER);
        busy = false;
        rt_mutex_release(&state_lock);
    }
}

/* 建立状态锁、请求信号量和初始结果，启动外设工作线程。 */
int lab_init(void)
{
    rt_thread_t thread;
    if (rt_mutex_init(&state_lock, "lab_lock", RT_IPC_FLAG_PRIO) != RT_EOK) return -1;
    if (rt_sem_init(&request_ready, "lab_job", 0, RT_IPC_FLAG_FIFO) != RT_EOK) return -1;
    for (int i = 0; i < LAB_COUNT; ++i) {
        results[i].status = LAB_IDLE;
        rt_snprintf(results[i].value, sizeof(results[i].value), "Ready to test");
        rt_snprintf(results[i].detail, sizeof(results[i].detail), "Choose an action below to begin.");
    }
    lab_game_sound_init();
    thread = rt_thread_create("lab_io", worker, RT_NULL, 4096, 22, 10);
    if (!thread) return -1;
    return rt_thread_startup(thread);
}
