#ifndef AIRTIZEN_NACL_AUDIO_QUEUE_H
#define AIRTIZEN_NACL_AUDIO_QUEUE_H
#include <stddef.h>
#include <stdint.h>
#include <pthread.h>
#ifdef __cplusplus
extern "C" {
#endif
#define AIRTIZEN_AUDIO_FRAMES 16384
/* One producer/one callback, bounded PCM queue. PPAPI output is always stereo. */
typedef struct {
    pthread_mutex_t mutex;
    int16_t samples[AIRTIZEN_AUDIO_FRAMES * 2];
    size_t read_frame, count;
    unsigned channels;
    int open;
    uint64_t rendered_frames, underruns;
} airtizen_audio_queue;
void airtizen_audio_queue_init(airtizen_audio_queue *queue);
void airtizen_audio_queue_destroy(airtizen_audio_queue *queue);
void airtizen_audio_queue_open(airtizen_audio_queue *queue, unsigned channels);
void airtizen_audio_queue_close(airtizen_audio_queue *queue);
int airtizen_audio_queue_write(airtizen_audio_queue *queue, const void *pcm, size_t bytes);
void airtizen_audio_queue_render(airtizen_audio_queue *queue, void *stereo, size_t bytes);
#ifdef __cplusplus
}
#endif
#endif
