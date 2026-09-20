/*
 * lab_mqtt.h — MQTT 测试器接口：保存协议阶段和有界收发缓冲，不依赖 RTOS 或 socket。
 */
#ifndef LAB_MQTT_H
#define LAB_MQTT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define LAB_MQTT_HOST "test.mosquitto.org"
#define LAB_MQTT_PORT 1883
/* 单次 MQTT 3.1.1 / QoS 0 回环，不是通用 MQTT 客户端。 */
typedef struct {
    uint8_t tx[256], rx[384];
    size_t tx_len, tx_sent, rx_len;
    unsigned phase;
    bool connected, subscribed, published, matched, done;
    const char *error;
    char client[24], topic[64], payload[64];
} lab_mqtt_t;
/* 初始化本轮主题、载荷及待发送 CONNECT 报文。 */
void lab_mqtt_start(lab_mqtt_t *m,const char *client,uint32_t nonce);
/* 仅在 tx 中整包数据发送完后调用，推进协议阶段。 */
void lab_mqtt_sent(lab_mqtt_t *m);
/* 输入收到的 TCP 字节；允许分包，零长度调用可继续解析已缓存的报文。 */
bool lab_mqtt_feed(lab_mqtt_t *m,const uint8_t *data,size_t len);
#endif
