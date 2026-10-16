#include "airtizen_core.h"
#include "airtizen_json.h"
#include "airtizen_video.h"
#include "airtizen_metadata.h"
#include "airtizen_photo.h"
#include "airtizen_playback.h"
#include "raop.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <time.h>

int airtizen_audio_open(int sample_rate, int channels, int bits_per_sample);
int airtizen_audio_write(const void *pcm, size_t bytes);
void airtizen_audio_close(void);

static raop_t *g_raop;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_audio_lock = PTHREAD_MUTEX_INITIALIZER;
static airtizen_core_state g_state;
static float g_gain = 1.0f;
static unsigned g_sessions;
#define LOG_COUNT 32
#define LOG_LENGTH 180
static char g_logs[LOG_COUNT][LOG_LENGTH];
static unsigned g_log_next, g_log_count;

static void log_cb(void *cls, int level, const char *msg) {
    (void)cls;
    if (!msg || level > RAOP_LOG_INFO) return;
    pthread_mutex_lock(&g_lock);
    snprintf(g_logs[g_log_next], LOG_LENGTH, "%ld [%d] %.145s", (long)time(NULL), level, msg);
    g_log_next = (g_log_next + 1) % LOG_COUNT;
    if (g_log_count < LOG_COUNT) ++g_log_count;
    pthread_mutex_unlock(&g_lock);
}
static void fail(const char *error) {
    pthread_mutex_lock(&g_lock);
    snprintf(g_state.last_error, sizeof(g_state.last_error), "%s", error);
    snprintf(g_state.audio, sizeof(g_state.audio), "failed");
    pthread_mutex_unlock(&g_lock);
    log_cb(NULL, RAOP_LOG_ERR, error);
}
static void cb_metadata(void *cls,void *session,const void *data,int size,const char *name,const char *id){
    (void)cls;(void)session;(void)name;(void)id;if(size>0 && airtizen_metadata_parse(data,(size_t)size)<0)log_cb(NULL,RAOP_LOG_WARNING,"Rejected malformed music metadata");
}
static void cb_artwork(void *cls,void *session,const void *data,int size,const char *name,const char *id){
    (void)cls;(void)session;(void)name;(void)id;if(size>0 && airtizen_metadata_artwork(data,(size_t)size)<0)log_cb(NULL,RAOP_LOG_WARNING,"Rejected invalid/oversized artwork");
}
static void cb_progress(void *cls,void *session,unsigned start,unsigned current,unsigned end,const char *name,const char *id){
    (void)cls;(void)session;(void)name;(void)id;airtizen_metadata_progress(start,current,end);
}
static void cb_audio_format(void *cls, unsigned ct) {
    (void)cls;
    pthread_mutex_lock(&g_lock);
    snprintf(g_state.codec, sizeof(g_state.codec), "%s", ct==2 ? "ALAC" : ct==4 ? "AAC-LC" : "AAC-ELD");
    pthread_mutex_unlock(&g_lock);
    log_cb(NULL,RAOP_LOG_INFO,ct==2 ? "Negotiated lossless ALAC" : ct==4 ? "Negotiated AAC-LC" : "Negotiated AAC-ELD");
}
static void cb_pairing_verified(void *cls) {
    (void)cls;
    pthread_mutex_lock(&g_lock); g_state.paired = 1; pthread_mutex_unlock(&g_lock);
    log_cb(NULL, RAOP_LOG_INFO, "Pair-verify signature verified");
}
static void cb_connected(void *cls, const char *name, const char *id) {
    (void)cls; (void)id;
    pthread_mutex_lock(&g_lock);
    ++g_sessions;
    g_state.connected = 1;
    snprintf(g_state.device, sizeof(g_state.device), "%s", name ? name : "AirPlay device");
    pthread_mutex_unlock(&g_lock);
    log_cb(NULL, RAOP_LOG_INFO, "Client connected");
}
static void cb_audio_flush(void *cls, void *session, const char *name, const char *id) {
    (void)cls; (void)session; (void)name; (void)id;
    pthread_mutex_lock(&g_audio_lock);
    airtizen_audio_close();
    pthread_mutex_unlock(&g_audio_lock);
    pthread_mutex_lock(&g_lock);
    snprintf(g_state.audio, sizeof(g_state.audio), "idle");
    pthread_mutex_unlock(&g_lock);
}
static void cb_disconnected(void *cls, const char *name, const char *id) {
    int last;
    pthread_mutex_lock(&g_lock);
    if(g_sessions)--g_sessions;
    last=g_sessions==0;
    if(last){g_state.connected=0;g_state.paired=0;g_state.device[0]=0;}
    pthread_mutex_unlock(&g_lock);
    if(last){cb_audio_flush(cls,NULL,name,id);airtizen_metadata_reset();}
    log_cb(NULL,RAOP_LOG_INFO,"Client stream disconnected");
}
static void cb_video_flush(void *cls){(void)cls;airtizen_video_reset();}
static void cb_audio_process(void *cls, pcm_data_struct *data, const char *name, const char *id) {
    int rc;
    int16_t scaled[4096];
    size_t offset = 0, length;
    float gain;
    (void)cls; (void)id;
    if (!data || !data->data || data->data_len <= 0) return;
    if (data->bits_per_sample != 16 || data->channels < 1 || data->channels > 2 ||
        data->sample_rate < 8000 || data->sample_rate > 192000 ||
        data->data_len % (2 * data->channels)) { fail("invalid_pcm_format"); return; }
    pthread_mutex_lock(&g_lock); gain = g_gain; pthread_mutex_unlock(&g_lock);
    pthread_mutex_lock(&g_audio_lock);
    rc = airtizen_audio_open((int)data->sample_rate, data->channels, data->bits_per_sample);
    if (rc != 0) { pthread_mutex_unlock(&g_audio_lock); fail("audio_open_failed"); return; }
    length = (size_t)data->data_len;
    while (offset < length) {
        size_t bytes = length - offset, i, written = 0;
        if (bytes > sizeof(scaled)) bytes = sizeof(scaled);
        for (i = 0; i < bytes / 2; ++i) {
            int16_t sample;
            memcpy(&sample, (const unsigned char *)data->data + offset + i * 2, 2);
            scaled[i] = (int16_t)(sample * gain);
        }
        while (written < bytes) {
            rc = airtizen_audio_write((unsigned char *)scaled + written, bytes - written);
            if (rc <= 0 || (size_t)rc > bytes - written) {
                pthread_mutex_unlock(&g_audio_lock); fail("audio_write_failed"); return;
            }
            written += (size_t)rc;
        }
        offset += bytes;
    }
    pthread_mutex_unlock(&g_audio_lock);
    pthread_mutex_lock(&g_lock);
    g_state.connected = 1;
    snprintf(g_state.device, sizeof(g_state.device), "%s", name ? name : "AirPlay device");
    g_state.sample_rate = data->sample_rate; g_state.audio_bytes += length;
#if defined(__TIZEN__) || defined(__native_client__)
    snprintf(g_state.audio, sizeof(g_state.audio), "playing");
#else
    snprintf(g_state.audio, sizeof(g_state.audio), "host_stub");
#endif
    g_state.last_error[0] = 0;
    pthread_mutex_unlock(&g_lock);
}
static void cb_video_process(void *cls, h264_decode_struct *data, const char *name, const char *id) {
    int result;
    (void)cls; (void)id;
    if (!data || !data->data || data->data_len <= 0) return;
    result=data->frame_type==0 ? airtizen_video_configure(data->data,(size_t)data->data_len) :
        airtizen_video_frame(data->data,(size_t)data->data_len,data->pts);
    if(data->frame_type==0 && result==0){airtizen_photo_reset();airtizen_playback_stop();}
    pthread_mutex_lock(&g_lock);
    if(data->frame_type!=0)++g_state.video_frames;
    if(result<0) snprintf(g_state.last_error,sizeof(g_state.last_error),"invalid_video_stream");
    else { g_state.connected=1;snprintf(g_state.device,sizeof(g_state.device),"%s",name ? name : "AirPlay device"); }
    pthread_mutex_unlock(&g_lock);
    if(result<0)log_cb(NULL,RAOP_LOG_ERR,"Video input or MP4 packaging failed");
}
static void cb_audio_set_volume(void *cls, void *session, float volume, const char *name, const char *id) {
    (void)cls; (void)session; (void)name; (void)id;
    if (!isfinite(volume)) return;
    if (volume > 0) volume = 0;
    pthread_mutex_lock(&g_lock);
    g_gain = volume <= -144 ? 0 : powf(10.0f, volume / 20.0f);
    pthread_mutex_unlock(&g_lock);
}
int airtizen_core_start(const char *device_name) {
    unsigned short port = 5000;
    int rc;
    raop_callbacks_t cb;
    (void)device_name;
    if (g_raop) return 0;
    memset(&cb, 0, sizeof(cb));
    cb.video_flush=cb_video_flush;
    cb.audio_set_metadata=cb_metadata;cb.audio_set_coverart=cb_artwork;cb.audio_set_progress=cb_progress;
    cb.audio_format = cb_audio_format;
    cb.pairing_verified = cb_pairing_verified;
    cb.connected = cb_connected; cb.disconnected = cb_disconnected;
    cb.audio_process = cb_audio_process; cb.video_process = cb_video_process;
    cb.audio_flush = cb_audio_flush; cb.audio_set_volume = cb_audio_set_volume;
    pthread_mutex_lock(&g_lock);
    memset(&g_state, 0, sizeof(g_state)); g_gain = 1;g_sessions=0;
    snprintf(g_state.audio, sizeof(g_state.audio), "idle");
    snprintf(g_state.codec, sizeof(g_state.codec), "AAC-ELD");
    pthread_mutex_unlock(&g_lock);
    g_raop = raop_init(4, &cb);
    if (!g_raop) { fail("raop_init_failed"); return -1; }
    raop_set_log_level(g_raop, RAOP_LOG_INFO);
    raop_set_log_callback(g_raop, log_cb, NULL);
    rc = raop_start(g_raop, &port);
    if (rc < 0) { raop_destroy(g_raop); g_raop = NULL; fail("raop_start_failed"); return rc; }
    pthread_mutex_lock(&g_lock); g_state.running = 1; pthread_mutex_unlock(&g_lock);
    log_cb(NULL, RAOP_LOG_INFO, "RAOP listener started");
    return 0;
}
void airtizen_core_stop(void) {
    /* Join producer threads before closing the audio device. */
    if (g_raop) { raop_stop(g_raop); raop_destroy(g_raop); g_raop = NULL; }
    cb_disconnected(NULL, NULL, NULL);
    airtizen_video_reset();
    airtizen_metadata_reset();
    pthread_mutex_lock(&g_lock); g_state.running = 0; pthread_mutex_unlock(&g_lock);
    log_cb(NULL, RAOP_LOG_INFO, "RAOP listener stopped");
}
void airtizen_core_snapshot(airtizen_core_state *out) {
    pthread_mutex_lock(&g_lock); *out = g_state; pthread_mutex_unlock(&g_lock);
}
void airtizen_core_logs_json(char *out, size_t capacity) {
    unsigned i;
    size_t used = 1;
    if (capacity < 3) return;
    out[0] = '[';
    pthread_mutex_lock(&g_lock);
    for (i = 0; i < g_log_count; ++i) {
        char quoted[LOG_LENGTH * 6 + 3];
        unsigned idx = (g_log_next + LOG_COUNT - g_log_count + i) % LOG_COUNT;
        int n = airtizen_json_quote(quoted, sizeof(quoted), g_logs[idx]);
        if (n < 0 || used + (size_t)n + 3 > capacity) break;
        if (i) out[used++] = ',';
        memcpy(out + used, quoted, (size_t)n); used += (size_t)n;
    }
    pthread_mutex_unlock(&g_lock);
    out[used++] = ']'; out[used] = 0;
}
