/*
 * lab_ble.h — BLE 公共接口：定义界面命令、连接快照及广播/回环数据处理函数。
 */
#ifndef LAB_BLE_H
#define LAB_BLE_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define LAB_BLE_DEVICES 30
#define LAB_BLE_PAYLOAD 20
/* UUID 文本以手机显示的字节顺序为准，属性限定 20 字节以兼容默认 MTU。 */
#define LAB_BLE_SERVICE_UUID "7bc10000-8a52-4c65-9c31-5349464c4901"
typedef struct {
    uint8_t address[6], type;
    int8_t rssi;
    uint8_t phy, info;
    char name[25];
    unsigned reports;
} lab_ble_device_t;
typedef struct {
    bool ready, scanning, advertising, connecting, connected, stopping, subscribed, echo;
    bool encrypted, bonded, pair_pending;
    uint8_t conn_index, tx_phy, rx_phy;
    unsigned mtu, interval, device_count, rx_bytes, tx_bytes, loop_ok, dropped, revision;
    uint32_t passkey;
    char name[24], address[18], peer[18], message[128], last_rx[61];
    lab_ble_device_t devices[LAB_BLE_DEVICES];
} lab_ble_snapshot_t;
typedef enum { LAB_BLE_SCAN, LAB_BLE_ADVERTISE, LAB_BLE_CONNECT, LAB_BLE_SEND,
    LAB_BLE_ECHO, LAB_BLE_PAIR, LAB_BLE_ACCEPT, LAB_BLE_REJECT, LAB_BLE_FORGET,
    LAB_BLE_PHY1, LAB_BLE_PHY2 } lab_ble_command_t;
/* 创建 BLE 服务任务，栈就绪后再注册 GATT 与广播。 */
int lab_ble_init(void);
/* 异步提交命令；返回 true 仅表示入队成功，结果通过快照读取。 */
bool lab_ble_command(lab_ble_command_t command, unsigned index);
/* 停止本页无线活动并使旧命令失效，不清空绑定或关闭整个共享栈。 */
void lab_ble_stop(void);
/* 读取受锁保护的状态副本，供界面显示。 */
void lab_ble_snapshot(lab_ble_snapshot_t *snapshot);
/* 在 LVGL 线程打开 BLE 测试层。 */
void lab_ble_open(void);
/* 与 SDK 无关的边界检查，便于主机验证。 */
void lab_ble_name(const uint8_t *data, size_t length, char *name, size_t capacity);
bool lab_ble_add_device(lab_ble_snapshot_t *state, const lab_ble_device_t *device);
void lab_ble_packet(uint8_t packet[12], uint32_t sequence);
bool lab_ble_packet_matches(const uint8_t *data, unsigned length, const uint8_t expected[12]);
#endif
