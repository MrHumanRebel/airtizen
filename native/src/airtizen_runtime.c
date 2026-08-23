#include "airtizen_runtime.h"
#include "airtizen_core.h"
#include "airtizen_mdns.h"
#include "airtizen_control_http.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifndef _WIN32
#include <unistd.h>
#else
#include <windows.h>
#endif

static char g_runtime_status[8192];
static int g_started = 0;
static unsigned short g_raop_port = 5000;
static char g_receiver_name[128] = "AirTizen TV";

static void set_runtime_status(const char *service,
                               const char *airplay_state,
                               int discoverable,
                               int connected,
                               const char *device,
                               const char *audio_state,
                               const char *detail,
                               const char *last_error)
{
    snprintf(g_runtime_status, sizeof(g_runtime_status),
        "{\"ok\":true,"
        "\"service\":\"%s\"," 
        "\"receiverName\":\"%s\"," 
        "\"protocols\":{"
          "\"airplay\":{\"enabled\":true,\"state\":\"%s\",\"discoverable\":%s,\"connected\":%s,\"device\":\"%s\",\"lastError\":\"%s\"},"
          "\"cast\":{\"enabled\":false,\"state\":\"unavailable\",\"discoverable\":false,\"connected\":false,\"device\":\"\",\"lastError\":\"Requires official SDK/certification\"},"
          "\"spotify\":{\"enabled\":false,\"state\":\"unavailable\",\"discoverable\":false,\"connected\":false,\"device\":\"\",\"lastError\":\"Spotify Connect backend not built; use Spotify via AirPlay\"},"
          "\"dlna\":{\"enabled\":false,\"state\":\"unavailable\",\"discoverable\":false,\"connected\":false,\"device\":\"\",\"lastError\":\"DLNA backend not built in this build\"}"
        "},"
        "\"audio\":{\"state\":\"%s\",\"sampleRate\":44100,\"channels\":2,\"bitsPerSample\":16,\"buffer\":\"adaptive\",\"lastError\":\"%s\"},"
        "\"nowPlaying\":{\"protocol\":\"%s\",\"app\":\"\",\"title\":\"\",\"artist\":\"\",\"album\":\"\",\"artwork\":\"\"},"
        "\"status\":{\"mdns\":\"%s\",\"raop\":\"%s\",\"pair\":\"idle\",\"audio\":\"%s\",\"connected\":%s,\"device\":\"%s\",\"codec\":\"ALAC/AAC\",\"rate\":\"44100\",\"buffer\":\"adaptive\",\"detail\":\"%s\",\"lastError\":\"%s\"},"
        "\"logs\":[]}",
        service, g_receiver_name,
        airplay_state, discoverable ? "true" : "false", connected ? "true" : "false", device ? device : "", last_error ? last_error : "",
        audio_state ? audio_state : "closed", last_error ? last_error : "",
        connected ? "airplay" : "", discoverable ? "advertising" : "offline", g_started ? "listening" : "offline",
        audio_state ? audio_state : "idle", connected ? "true" : "false", device ? device : "", detail ? detail : "", last_error ? last_error : "");
}

int airtizen_runtime_start(const char *device_name) {
    int rc;
    const char *name = (device_name && *device_name) ? device_name : "AirTizen TV";
    snprintf(g_receiver_name, sizeof(g_receiver_name), "%s", name);
    if (g_started) {
        set_runtime_status("running", "advertising", 1, 0, "", "opening", "Runtime already running.", "");
        return 0;
    }
    set_runtime_status("running", "starting", 0, 0, "", "opening", "Starting RAOP and mDNS backends.", "");
    rc = airtizen_core_start(name);
    if (rc != 0) {
        set_runtime_status("running", "failed", 0, 0, "", "failed", "RAOP core start failed.", "raop_start_failed");
        return rc;
    }
    rc = airtizen_mdns_start(name, "A1B2C3D4E5F6", g_raop_port);
    if (rc != 0) {
        airtizen_core_stop();
        set_runtime_status("running", "failed", 0, 0, "", "failed", "mDNS advertiser start failed.", "mdns_start_failed");
        return rc;
    }
    g_started = 1;
    set_runtime_status("running", "advertising", 1, 0, "", "ready", "AirPlay/RAOP listening and mDNS advertising.", "");
    return 0;
}

void airtizen_runtime_stop(void) {
    if (g_started) {
        airtizen_mdns_stop();
        airtizen_core_stop();
        g_started = 0;
    }
    set_runtime_status("offline", "offline", 0, 0, "", "closed", "Runtime stopped.", "");
}

const char *airtizen_runtime_status_json(void) {
    if (!g_runtime_status[0]) {
        set_runtime_status("offline", "offline", 0, 0, "", "closed", "Runtime not started.", "");
    }
    return g_runtime_status;
}

#ifndef AIRTIZEN_RUNTIME_NO_MAIN
int main(int argc, char **argv) {
    const char *name = argc > 1 ? argv[1] : "AirTizen TV";
    int rc = airtizen_control_http_start(45110);
    if (rc != 0) {
        fprintf(stderr, "control HTTP start failed: %d\n", rc);
        return rc;
    }
    rc = airtizen_runtime_start(name);
    puts(airtizen_runtime_status_json());
    fflush(stdout);
    if (rc != 0) { airtizen_control_http_stop(); return rc; }
    for (;;) {
#ifndef _WIN32
        sleep(1000);
#else
        Sleep(1000);
#endif
    }
    return 0;
}
#endif
