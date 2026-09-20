/*
 * touch_decode.c — FT6146 触摸解码：过滤无效坐标、释放事件和重复 ID，最多提取两个真实触点。
 */
#include "lab.h"
#include <string.h>

/* FT6146: 从 0x02 起，状态字节后是两个 6 字节触点记录。 */
bool lab_touch_decode(const uint8_t data[13], lab_touch_frame_t *frame)
{
    memset(frame, 0, sizeof(*frame));
    unsigned count = data[0] & 15;
    if (count > 2) return false;
    for (unsigned i = 0; i < 2 && frame->count < count; ++i) {
        const uint8_t *p = data + 1 + i * 6;
        unsigned event = p[0] >> 6;
        if (event != 0 && event != 2) continue;
        lab_touch_point_t *point = &frame->points[frame->count];
        point->x = ((p[0] & 15) << 8) | p[1];
        point->y = ((p[2] & 15) << 8) | p[3];
        point->id = p[2] >> 4;
        if (point->x >= 390 || point->y >= 450 || point->id == 15) continue;
        if (frame->count && point->id == frame->points[0].id) continue;
        frame->count++;
    }
    return frame->count == count;
}
