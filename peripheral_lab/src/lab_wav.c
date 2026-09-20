/*
 * lab_wav.c — WAV 格式解析：检查 RIFF 块及 PCM 格式，返回原数据中的音频片段而不复制整段。
 */
#include "lab_wav.h"
#include <string.h>
#include <limits.h>
static unsigned u16(const uint8_t *p) { return p[0] | (unsigned)p[1] << 8; }
static uint32_t u32(const uint8_t *p) { return u16(p) | (uint32_t)u16(p + 2) << 16; }
/* 逐块检查长度和格式，仅接受 16 kHz、单声道、16 位 PCM。 */
bool lab_wav_decode(const uint8_t *file, size_t size, const uint8_t **pcm, unsigned *samples)
{
    *pcm = NULL; *samples = 0;
    if (!file || size < 12 || memcmp(file, "RIFF", 4) || memcmp(file + 8, "WAVE", 4)) return false;
    uint32_t riff_size = u32(file + 4);
    if (riff_size < 4 || riff_size > size - 8) return false;
    size_t end = (size_t)riff_size + 8, offset = 12;
    bool format = false;
    const uint8_t *data = NULL;
    unsigned count = 0;
    while (offset < end) {
        if (end - offset < 8) return false;
        const uint8_t *chunk = file + offset;
        uint32_t length = u32(chunk + 4);
        offset += 8;
        if (length > end - offset) return false;
        if (!memcmp(chunk, "fmt ", 4)) {
            const uint8_t *f = file + offset;
            if (format || length < 16 || u16(f) != 1 || u16(f+2) != 1 ||
                u32(f+4) != 16000 || u32(f+8) != 32000 || u16(f+12) != 2 || u16(f+14) != 16) return false;
            format = true;
        } else if (!memcmp(chunk, "data", 4)) {
            if (data || !length || (length & 1) || length / 2 > UINT_MAX) return false;
            data = file + offset; count = length / 2;
        }
        offset += length;
        if (length & 1) {
            if (offset == end) return false;
            offset++; /* RIFF 奇数长度块补齐到偶数字节。 */
        }
    }
    if (!format || !data) return false;
    *pcm = data; *samples = count;
    return true;
}
