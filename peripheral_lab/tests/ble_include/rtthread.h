#ifndef LAB_BLE_HOST_RT_H
#define LAB_BLE_HOST_RT_H
#include "../core_include/rtthread.h"
#include <stdlib.h>
#define rt_malloc malloc
#define rt_free free
#define RT_ENOMEM 12
typedef void *rt_timer_t;
typedef void *rt_mq_t;
rt_mq_t rt_mq_create(const char *, size_t, unsigned, unsigned);
int rt_mq_send(rt_mq_t,const void *,size_t);
int rt_mq_recv(rt_mq_t,void *,size_t,int);
int rt_mq_delete(rt_mq_t);
#endif
