#include "airtizen_runtime.h"
#include "airtizen_core.h"
#include "airtizen_mdns.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifndef _WIN32
#include <unistd.h>
#else
#include <windows.h>
#endif

static char g_runtime_status[4096];
static int g_started = 0;
static unsigned short g_raop_port = 5000;

static void set_runtime_status(const char *mdns, const char *raop, const char *detail) {
    snprintf(g_runtime_status, sizeof(g_runtime_status),
        "{\"ok\":true,\"status\":{"
        "\"mdns\":\"%s\","
        "\"raop\":\"%s\","
        "\"pair\":\"idle\","
        "\"audio\":\"idle\","
        "\"connected\":false,"
        "\"device\":\"\","
        "\"codec\":\"ALAC/AAC\","
        "\"rate\":\"44100\","
        "\"buffer\":\"adaptive\","
        "\"detail\":\"%s\"}}",
        mdns ? mdns : "unknown",
        raop ? raop : "unknown",
        detail ? detail : "unknown");
}

int airtizen_runtime_start(const char *device_name) {
    int rc;
    const char *name = (device_name && *device_name) ? device_name : "AirTizen TV";
    if (g_started) {
        set_runtime_status("advertising", "listening", "AirTizen native runtime is already running.");
        return 0;
    }
    rc = airtizen_core_start(name);
    if (rc != 0) {
        set_runtime_status("stopped", "failed", "RAOP core failed to start.");
        return rc;
    }
    rc = airtizen_mdns_start(name, "A1B2C3D4E5F6", g_raop_port);
    if (rc != 0) {
        airtizen_core_stop();
        set_runtime_status("failed", "stopped", "mDNS advertiser failed to start.");
        return rc;
    }
    g_started = 1;
    set_runtime_status("advertising", "listening", "RAOP core is listening and mDNS is advertising _raop._tcp.local plus _airplay._tcp.local.");
    return 0;
}

void airtizen_runtime_stop(void) {
    if (g_started) {
        airtizen_mdns_stop();
        airtizen_core_stop();
        g_started = 0;
    }
    set_runtime_status("stopped", "stopped", "AirTizen native runtime stopped.");
}

const char *airtizen_runtime_status_json(void) {
    if (!g_runtime_status[0]) {
        set_runtime_status("stopped", "stopped", "AirTizen native runtime has not been started.");
    }
    return g_runtime_status;
}

int main(int argc, char **argv) {
    const char *name = argc > 1 ? argv[1] : "AirTizen TV";
    int rc = airtizen_runtime_start(name);
    puts(airtizen_runtime_status_json());
    fflush(stdout);
    if (rc != 0) return rc;
    for (;;) {
#ifndef _WIN32
        sleep(1000);
#else
        Sleep(1000);
#endif
    }
    return 0;
}
