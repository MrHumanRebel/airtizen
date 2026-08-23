#include "airtizen_core.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "raop.h"

int airtizen_audio_open(int sample_rate, int channels, int bits_per_sample);
int airtizen_audio_write(const void *pcm, size_t bytes);
void airtizen_audio_close(void);

static raop_t *g_raop = NULL;
static char g_status[2048];

static void set_status(const char *msg) {
    snprintf(g_status, sizeof(g_status), "{\"receiver\":\"AirTizen\",\"status\":\"%s\"}", msg ? msg : "unknown");
}

static void log_cb(void *cls, int level, const char *msg) {
    (void)cls; (void)level;
    if (msg) set_status(msg);
}

static void cb_connected(void* cls, const char* remoteName, const char* remoteDeviceId) {
    (void)cls; (void)remoteDeviceId;
    char buf[256]; snprintf(buf, sizeof(buf), "connected:%s", remoteName ? remoteName : "iOS device"); set_status(buf);
}
static void cb_disconnected(void* cls, const char* remoteName, const char* remoteDeviceId) {
    (void)cls; (void)remoteName; (void)remoteDeviceId;
    airtizen_audio_close();
    set_status("disconnected");
}
static void cb_audio_process(void *cls, pcm_data_struct *data, const char* remoteName, const char* remoteDeviceId) {
    size_t bytes;
    (void)cls; (void)remoteName; (void)remoteDeviceId;
    if (!data || !data->data || data->data_len <= 0) return;
    if (airtizen_audio_open((int)data->sample_rate, (int)data->channels,
                            (int)data->bits_per_sample) != 0) {
        set_status("audio_open_failed");
        return;
    }
    bytes = (size_t)data->data_len;
    if (airtizen_audio_write(data->data, bytes) < 0) set_status("audio_write_failed");
    else set_status("audio_pcm_playing");
}
static void cb_video_process(void *cls, h264_decode_struct *data, const char* remoteName, const char* remoteDeviceId) {
    (void)cls; (void)data; (void)remoteName; (void)remoteDeviceId;
}
static void cb_audio_flush(void *cls, void *session, const char* remoteName, const char* remoteDeviceId) {
    (void)cls; (void)session; (void)remoteName; (void)remoteDeviceId;
    airtizen_audio_close();
    set_status("audio_flush");
}
static void cb_audio_set_volume(void *cls, void *session, float volume, const char* remoteName, const char* remoteDeviceId) {
    (void)cls; (void)session; (void)remoteName; (void)remoteDeviceId; char b[64]; snprintf(b,sizeof(b),"volume:%0.2f",volume); set_status(b);
}

int airtizen_core_start(const char *device_name) {
    (void)device_name;
    if (g_raop) return 0;
    raop_callbacks_t cb;
    memset(&cb, 0, sizeof(cb));
    cb.connected = cb_connected;
    cb.disconnected = cb_disconnected;
    cb.audio_process = cb_audio_process;
    cb.video_process = cb_video_process;
    cb.audio_flush = cb_audio_flush;
    cb.audio_set_volume = cb_audio_set_volume;
    g_raop = raop_init(4, &cb);
    if (!g_raop) { set_status("raop_init_failed"); return -1; }
    raop_set_log_level(g_raop, RAOP_LOG_DEBUG);
    raop_set_log_callback(g_raop, log_cb, NULL);
    unsigned short port = 5000;
    int rc = raop_start(g_raop, &port);
    /* Donor httpd returns 1 on success and a negative value on failure. */
    if (rc < 0) { raop_destroy(g_raop); g_raop = NULL; set_status("raop_start_failed"); return rc; }
    set_status("raop_started");
    return 0;
}

void airtizen_core_stop(void) {
    airtizen_audio_close();
    if (g_raop) { raop_stop(g_raop); raop_destroy(g_raop); g_raop = NULL; }
    set_status("stopped");
}

const char *airtizen_core_status_json(void) {
    if (!g_status[0]) set_status("created");
    return g_status;
}
