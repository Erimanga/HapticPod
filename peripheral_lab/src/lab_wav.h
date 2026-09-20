/*
 * lab_wav.h — WAV 接口：提供格式校验与内置测试音播放入口。
 */
#ifndef LAB_WAV_H
#define LAB_WAV_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* 本示例支持 16 kHz / 16-bit / mono PCM；跳过未知 RIFF 块。 */
bool lab_wav_decode(const uint8_t *file, size_t size, const uint8_t **pcm, unsigned *samples);
void lab_wav_run(unsigned volume);
#endif
