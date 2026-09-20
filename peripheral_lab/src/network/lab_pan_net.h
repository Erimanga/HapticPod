/*
 * lab_pan_net.h — PAN 网络层接口：由 PAN 服务任务调用，启动、停止并轮询一次联网测试。
 */
#ifndef LAB_PAN_NET_H
#define LAB_PAN_NET_H
#include "lab_pan.h"
/* 创建接收 tcpip 线程回调结果的队列。 */
int lab_pan_net_init(void);
/* PAN 链路建立后开启地址监测。 */
void lab_pan_net_activate(void);
/* 关闭连接、作废旧异步结果并安排恢复默认路由。 */
void lab_pan_net_stop(void);
/* 启动一次 HTTP 检查，要求 PAN 已取得 IP。 */
void lab_pan_net_test(void);
/* 启动一次 MQTT 精确回环，与 HTTP 共用同一网络测试状态机。 */
void lab_pan_net_mqtt(void);
/* 由 PAN 任务周期调用，推进非阻塞收发并更新网络快照。 */
void lab_pan_net_poll(lab_pan_snapshot_t *snapshot);
#endif
