/*
 * lab_ble_model.c — BLE 数据处理：解析广播名称、合并扫描设备并构造回环测试包，不调用协议栈。
 */
#include "lab_ble.h"
#include <string.h>
/* 遍历广播数据项提取可显示的名称，优先使用完整名称。 */
void lab_ble_name(const uint8_t *data, size_t length, char *name, size_t capacity)
{
    if (!capacity) return;
    name[0] = 0;
    for (size_t pos=0; pos<length;) {
        size_t n = data[pos++];
        if (!n || n > length-pos) break;
        if (data[pos] == 8 || data[pos] == 9) {
            size_t copy = n-1 < capacity-1 ? n-1 : capacity-1;
            for (size_t i=0; i<copy; ++i) {
                uint8_t c = data[pos+1+i];
                name[i] = c >= 32 && c <= 126 ? c : '.';
            }
            name[copy] = 0;
            if (data[pos] == 9) return;
        }
        pos += n;
    }
}
/* 按地址和地址类型合并扫描报告，设备表满时拒绝新增。 */
bool lab_ble_add_device(lab_ble_snapshot_t *state, const lab_ble_device_t *device)
{
    unsigned i;
    for (i=0; i<state->device_count; ++i)
        if (state->devices[i].type == device->type && !memcmp(state->devices[i].address, device->address, 6)) break;
    if (i == LAB_BLE_DEVICES) return false;
    if (i == state->device_count) state->devices[state->device_count++] = *device;
    else {
        state->devices[i].rssi = device->rssi;
        state->devices[i].phy = device->phy;
        state->devices[i].info = device->info;
        if (device->name[0]) memcpy(state->devices[i].name, device->name, sizeof(device->name));
    }
    state->devices[i].reports++;
    return true;
}
static uint32_t checksum(const uint8_t *data)
{
    uint32_t h = 2166136261u;
    for (unsigned i=0; i<8; ++i) h = (h ^ data[i]) * 16777619u;
    return h;
}
/* 生成带序号和校验值的挑战包，区分本轮测试与旧数据。 */
void lab_ble_packet(uint8_t packet[12], uint32_t sequence)
{
    memcpy(packet, "LAB1", 4);
    for (unsigned i=0; i<4; ++i) packet[4+i] = sequence >> (8*i);
    uint32_t sum = checksum(packet);
    for (unsigned i=0; i<4; ++i) packet[8+i] = sum >> (8*i);
}
/* 仅把与本轮挑战包长度、内容都一致的数据认作回环。 */
bool lab_ble_packet_matches(const uint8_t *data, unsigned length, const uint8_t expected[12])
{
    return length == 12 && !memcmp(data, expected, 12);
}
