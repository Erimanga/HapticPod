#ifndef LAB_TEST_AUDIO_SERVER_H
#define LAB_TEST_AUDIO_SERVER_H
#include <stdint.h>
typedef void *audio_client_t;
typedef int audio_type_t;
typedef int audio_rwflag_t;
typedef int audio_device_e;
typedef int (*audio_server_callback_func)(int, void *, uint32_t);
#define AUDIO_TYPE_LOCAL_MUSIC 4
#define AUDIO_TX 1
#define AUDIO_DEVICE_SPEAKER 0
#define AUDIO_IOCTL_FLUSH_TIME_MS 1
typedef struct { uint32_t write_samplerate, write_cache_size; uint8_t write_channnel_num, write_bits_per_sample; } audio_parameter_t;
uint8_t audio_server_get_private_volume(audio_type_t type);
int audio_server_set_private_volume(audio_type_t type, uint8_t value);
audio_client_t audio_open2(audio_type_t, audio_rwflag_t, audio_parameter_t *, audio_server_callback_func, void *, audio_device_e);
int audio_write(audio_client_t, uint8_t *, uint32_t);
int audio_ioctl(audio_client_t, int, void *);
int audio_close(audio_client_t);
#endif
