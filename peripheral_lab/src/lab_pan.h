/*
 * lab_pan.h — 蓝牙联网公共接口：定义 PAN 控制命令及蓝牙、HTTP、MQTT 状态快照。
 */
#ifndef LAB_PAN_H
#define LAB_PAN_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef enum { PAN_BEGIN, PAN_RECONNECT, PAN_TEST, PAN_ACCEPT, PAN_REJECT, PAN_MQTT } lab_pan_command_t;
typedef struct {
    bool ready, enabled, acl, hid, pan, pairing, stopping, testing;
    bool ip_ok, dns_ok, tcp_ok, http_ok;
    bool mqtt_connected, mqtt_subscribed, mqtt_published, mqtt_ok;
    unsigned code, http_status, scan_mode;
    char name[32], address[18];
    char peer[18], ip[16], gateway[16], dns[16], remote[16], message[128];
} lab_pan_snapshot_t;
/* 建立经典蓝牙事件接收与 PAN 任务，在共享蓝牙栈启动前调用。 */
int lab_pan_init(void);
/* 异步提交配对、连接或联网测试命令，成功入队不等于操作完成。 */
bool lab_pan_command(lab_pan_command_t command);
/* 请求停止并保留绑定；断开是否完成以快照中的链路状态为准。 */
void lab_pan_stop(void);
/* 复制蓝牙及网络各阶段的结果，供 UI 定时读取。 */
void lab_pan_snapshot(lab_pan_snapshot_t *snapshot);
/* 在 LVGL 线程打开蓝牙联网测试层。 */
void lab_pan_open(void);
/* 仅解析响应首行，不把重定向或错误页当作联网通过。 */
int lab_pan_http_status(const char *data, size_t length);
#endif
