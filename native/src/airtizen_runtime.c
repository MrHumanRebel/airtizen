#include "airtizen_runtime.h"
#include "airtizen_core.h"
#include "airtizen_mdns.h"
#include "airtizen_control_http.h"
#include "airtizen_json.h"
#include "airtizen_video.h"
#include "airtizen_playback.h"
#include "airtizen_metadata.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_started;
static char g_error[80];
static char g_receiver_name[49] = "AirTizen TV";
/* Each caller owns a separate status buffer; serializing never races a callback. */
static __thread char g_json[32768];

int airtizen_runtime_start(const char *device_name) {
    int rc = 0;
    size_t i;
    const char *name = device_name && *device_name ? device_name : "AirTizen TV";
    if (strlen(name) > 48) return -1;
    for (i = 0; name[i]; ++i)
        if ((unsigned char)name[i] < 32 || (unsigned char)name[i] > 126 || name[i] == '.') return -1;
    pthread_mutex_lock(&g_lock);
    if (g_started) { pthread_mutex_unlock(&g_lock); return 0; }
    snprintf(g_receiver_name, sizeof(g_receiver_name), "%s", name);
    g_error[0] = 0;
    rc = airtizen_core_start(name);
    if (rc != 0) snprintf(g_error, sizeof(g_error), "raop_start_failed");
    else {
        rc = airtizen_mdns_start(name, NULL, 5000);
        if (rc != 0) {
            airtizen_core_stop();
            snprintf(g_error, sizeof(g_error), "mdns_start_failed");
        } else g_started = 1;
    }
    pthread_mutex_unlock(&g_lock);
    return rc;
}
void airtizen_runtime_stop(void) {
    pthread_mutex_lock(&g_lock);
    if (g_started) { airtizen_mdns_stop(); airtizen_core_stop(); airtizen_playback_stop(); g_started = 0; }
    g_error[0] = 0;
    pthread_mutex_unlock(&g_lock);
}
const char *airtizen_runtime_status_json(void) {
    airtizen_core_state state;
    airtizen_video_state video;
    airtizen_playback_state playback;
    airtizen_metadata_state metadata;
    char title[1540],artist[1540],album[1540];
    char receiver[300], device[800], error[800], audio[200], codec[128], mime[500], share_url[12300], logs[10000];
    int mdns;
    const char *airplay, *last_error;
    pthread_mutex_lock(&g_lock);
    airtizen_core_snapshot(&state);
    airtizen_video_snapshot(&video);
    airtizen_playback_snapshot(&playback);
    airtizen_metadata_snapshot(&metadata);
    airtizen_json_quote(title,sizeof(title),metadata.title);airtizen_json_quote(artist,sizeof(artist),metadata.artist);airtizen_json_quote(album,sizeof(album),metadata.album);
    airtizen_json_quote(share_url,sizeof(share_url),playback.url);
    airtizen_json_quote(mime,sizeof(mime),video.mime);
    mdns = airtizen_mdns_is_running();
    airplay = g_error[0] ? "failed" : state.connected ? "connected" : g_started ? "advertising" : "offline";
    last_error = g_error[0] ? g_error : state.last_error;
    airtizen_json_quote(receiver, sizeof(receiver), g_receiver_name);
    airtizen_json_quote(device, sizeof(device), state.device);
    airtizen_json_quote(error, sizeof(error), last_error);
    airtizen_json_quote(audio, sizeof(audio), state.audio[0] ? state.audio : "closed");
    airtizen_json_quote(codec,sizeof(codec),state.codec[0] ? state.codec : "AAC-ELD");
    airtizen_core_logs_json(logs, sizeof(logs));
    snprintf(g_json, sizeof(g_json),
        "{\"ok\":true,\"service\":\"%s\",\"receiverName\":%s,"
        "\"capabilities\":{\"audio\":true,\"mirroring\":true,\"videoStreaming\":true,\"drm\":false,\"productionReady\":false,\"audioOutput\":\"%s\"},"
        "\"protocols\":{\"airplay\":{\"enabled\":true,\"state\":\"%s\",\"discoverable\":%s,\"connected\":%s,\"device\":%s,\"lastError\":%s}},"
        "\"audio\":{\"state\":%s,\"sampleRate\":%u,\"bytesReceived\":%llu},"
        "\"video\":{\"available\":%s,\"generation\":%u,\"start\":%llu,\"latest\":%llu,\"width\":%u,\"height\":%u,\"mime\":%s,\"framesReceived\":%llu},"
        "\"nowPlaying\":{\"title\":%s,\"artist\":%s,\"album\":%s,\"artwork\":%u,\"positionSamples\":%u,\"durationSamples\":%u},"
        "\"playback\":{\"active\":%s,\"generation\":%u,\"command\":%u,\"url\":%s,\"fraction\":%.9g,\"rate\":%.9g,\"seek\":%.9g},"
        "\"status\":{\"mdns\":\"%s\",\"raop\":\"%s\",\"pair\":\"%s\",\"audio\":%s,\"connected\":%s,\"device\":%s,"
        "\"codec\":%s,\"rate\":\"%u\",\"buffer\":\"adaptive\",\"detail\":\"%s\",\"lastError\":%s},\"logs\":%s}",
        g_started ? "running" : g_error[0] ? "failed" : "offline", receiver,
#if defined(__native_client__)
        "nacl",
#elif defined(__TIZEN__)
        "tizen",
#else
        "host_stub",
#endif
        airplay, mdns ? "true" : "false", state.connected ? "true" : "false", device, error,
        audio, state.sample_rate, (unsigned long long)state.audio_bytes,
        video.available ? "true" : "false",video.generation,(unsigned long long)video.start,(unsigned long long)video.latest,video.width,video.height,mime,(unsigned long long)state.video_frames,
        title,artist,album,metadata.artwork,metadata.progress,metadata.total,
        playback.active ? "true" : "false",playback.generation,playback.command,share_url,playback.fraction,playback.rate,playback.seek,
        mdns ? "advertising" : "offline", state.running ? "listening" : "offline", state.paired ? "verified" : "idle", audio,
        state.connected ? "true" : "false", device, codec, state.sample_rate,
        g_error[0] ? "Receiver startup failed. Open diagnostics." : g_started ? "Receiver running. Open AirPlay on your Apple device." : "Receiver stopped.", error, logs);
    pthread_mutex_unlock(&g_lock);
    return g_json;
}
#ifndef AIRTIZEN_RUNTIME_NO_MAIN
static volatile sig_atomic_t g_stop;
static void stop_signal(int sig) { (void)sig; g_stop = 1; }
int main(int argc, char **argv) {
    int rc;
    signal(SIGINT, stop_signal); signal(SIGTERM, stop_signal); signal(SIGPIPE, SIG_IGN);
    rc = airtizen_runtime_start(argc > 1 ? argv[1] : "AirTizen TV");
    if (rc != 0) { fprintf(stderr, "runtime start failed: %d\n", rc); return 1; }
    rc = airtizen_control_http_start(45110);
    if (rc != 0) { airtizen_runtime_stop(); fprintf(stderr, "control HTTP start failed: %d\n", rc); return 1; }
    puts(airtizen_runtime_status_json()); fflush(stdout);
    while (!g_stop) sleep(1);
    airtizen_control_http_stop(); airtizen_runtime_stop();
    return 0;
}
#endif
