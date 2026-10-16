#ifndef AIRTIZEN_CORE_H
#define AIRTIZEN_CORE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    int running, connected, paired;
    uint32_t sample_rate;
    uint64_t audio_bytes, video_frames;
    char device[128], audio[32], codec[16], last_error[128];
} airtizen_core_state;
int airtizen_core_start(const char *device_name);
void airtizen_core_stop(void);
void airtizen_core_snapshot(airtizen_core_state *out);
/* JSON array of bounded recent logs. Caller owns the supplied buffer. */
void airtizen_core_logs_json(char *out, size_t capacity);
#ifdef __cplusplus
}
#endif
#endif
