#ifndef LAB_CORE_RTTHREAD_H
#define LAB_CORE_RTTHREAD_H
#include "../include/rtthread.h"
#include <pthread.h>
#include <stdint.h>
typedef uint32_t rt_tick_t;
typedef uint32_t rt_uint32_t;
typedef intptr_t rt_base_t;
typedef size_t rt_size_t;
rt_tick_t rt_tick_get(void);
rt_tick_t rt_tick_from_millisecond(unsigned ms);
#define RT_EEMPTY 4
#define RT_ERROR 1
#define RT_EIO 8
#define RT_ETIMEOUT 2
#define RT_EOK 0
#define RT_NULL NULL
#define RT_WAITING_FOREVER -1
#define RT_IPC_FLAG_PRIO 0
#define RT_IPC_FLAG_FIFO 0
#define rt_kprintf(...) ((void)0)
struct rt_mutex { pthread_mutex_t mutex; };
struct rt_semaphore { pthread_mutex_t mutex; pthread_cond_t cond; unsigned count; };
typedef struct host_thread *rt_thread_t;
int rt_mutex_init(struct rt_mutex *, const char *, unsigned);
int rt_mutex_take(struct rt_mutex *, int);
int rt_mutex_release(struct rt_mutex *);
int rt_sem_init(struct rt_semaphore *, const char *, unsigned, unsigned);
int rt_sem_take(struct rt_semaphore *, int);
int rt_sem_release(struct rt_semaphore *);
rt_thread_t rt_thread_create(const char *, void (*)(void *), void *, unsigned, unsigned, unsigned);
int rt_thread_startup(rt_thread_t);
int rt_thread_delete(rt_thread_t);
void rt_thread_mdelay(unsigned);
#endif
