/* 可选桌面网络探针：复用固件协议模块，socket 由 Python 提供。 */
#include "network/lab_mqtt.h"
static lab_mqtt_t state;
void probe_start(const char *id,unsigned nonce) { lab_mqtt_start(&state,id,nonce); }
const uint8_t *probe_tx(void) { return state.tx; }
unsigned probe_tx_len(void) { return state.tx_len; }
void probe_sent(void) { lab_mqtt_sent(&state); }
int probe_feed(const uint8_t *data,unsigned n) { return lab_mqtt_feed(&state,data,n); }
int probe_done(void) { return state.done; }
const char *probe_error(void) { return state.error; }
const char *probe_topic(void) { return state.topic; }
