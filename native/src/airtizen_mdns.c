#include "airtizen_mdns.h"

#include <arpa/inet.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define AIRTIZEN_MDNS_GROUP "224.0.0.251"
#define AIRTIZEN_MDNS_PORT 5353
#define AIRTIZEN_MDNS_MAX 1400

static pthread_t g_thread;
static int g_running = 0;
static int g_sock = -1;
static char g_name[96] = "AirTizen TV";
static char g_id[32] = "A1B2C3D4E5F6";
static unsigned short g_port = 5000;

static int u16(unsigned char *b, int p, unsigned short v) {
    b[p++] = (unsigned char)((v >> 8) & 255);
    b[p++] = (unsigned char)(v & 255);
    return p;
}

static int u32(unsigned char *b, int p, unsigned int v) {
    b[p++] = (unsigned char)((v >> 24) & 255);
    b[p++] = (unsigned char)((v >> 16) & 255);
    b[p++] = (unsigned char)((v >> 8) & 255);
    b[p++] = (unsigned char)(v & 255);
    return p;
}

static int name(unsigned char *b, int p, const char *n) {
    const char *s = n;
    while (*s) {
        const char *d = strchr(s, '.');
        int len = d ? (int)(d - s) : (int)strlen(s);
        if (len > 63) len = 63;
        b[p++] = (unsigned char)len;
        memcpy(b + p, s, len);
        p += len;
        if (!d) break;
        s = d + 1;
    }
    b[p++] = 0;
    return p;
}

static int txt(unsigned char *b, int p, const char *t) {
    int len = (int)strlen(t);
    if (len > 255) len = 255;
    b[p++] = (unsigned char)len;
    memcpy(b + p, t, len);
    return p + len;
}

static int ptr(unsigned char *b, int p, const char *service, const char *instance) {
    int lpos, start;
    p = name(b, p, service);
    p = u16(b, p, 12);
    p = u16(b, p, 1);
    p = u32(b, p, 120);
    lpos = p;
    p = u16(b, p, 0);
    start = p;
    p = name(b, p, instance);
    b[lpos] = (unsigned char)(((p - start) >> 8) & 255);
    b[lpos + 1] = (unsigned char)((p - start) & 255);
    return p;
}

static int srv(unsigned char *b, int p, const char *instance, unsigned short port, const char *host) {
    int lpos, start;
    p = name(b, p, instance);
    p = u16(b, p, 33);
    p = u16(b, p, 1);
    p = u32(b, p, 120);
    lpos = p;
    p = u16(b, p, 0);
    start = p;
    p = u16(b, p, 0);
    p = u16(b, p, 0);
    p = u16(b, p, port);
    p = name(b, p, host);
    b[lpos] = (unsigned char)(((p - start) >> 8) & 255);
    b[lpos + 1] = (unsigned char)((p - start) & 255);
    return p;
}

static int txt_raop(unsigned char *b, int p, const char *instance) {
    int lpos, start;
    p = name(b, p, instance);
    p = u16(b, p, 16);
    p = u16(b, p, 1);
    p = u32(b, p, 120);
    lpos = p;
    p = u16(b, p, 0);
    start = p;
    p = txt(b, p, "txtvers=1");
    p = txt(b, p, "ch=2");
    p = txt(b, p, "cn=0,1,2,3");
    p = txt(b, p, "da=true");
    p = txt(b, p, "et=0,3,5");
    p = txt(b, p, "md=0,1,2");
    p = txt(b, p, "pw=false");
    p = txt(b, p, "sr=44100");
    p = txt(b, p, "ss=16");
    p = txt(b, p, "tp=UDP");
    p = txt(b, p, "vn=65537");
    p = txt(b, p, "vs=220.68");
    p = txt(b, p, "sf=0x4");
    b[lpos] = (unsigned char)(((p - start) >> 8) & 255);
    b[lpos + 1] = (unsigned char)((p - start) & 255);
    return p;
}

static int txt_airplay(unsigned char *b, int p, const char *instance) {
    int lpos, start;
    p = name(b, p, instance);
    p = u16(b, p, 16);
    p = u16(b, p, 1);
    p = u32(b, p, 120);
    lpos = p;
    p = u16(b, p, 0);
    start = p;
    p = txt(b, p, "deviceid=A1:B2:C3:D4:E5:F6");
    p = txt(b, p, "features=0x5A7FFFF7,0x1E");
    p = txt(b, p, "flags=0x4");
    p = txt(b, p, "model=AppleTV3,2");
    p = txt(b, p, "srcvers=220.68");
    p = txt(b, p, "vv=2");
    b[lpos] = (unsigned char)(((p - start) >> 8) & 255);
    b[lpos + 1] = (unsigned char)((p - start) & 255);
    return p;
}

static int build_packet(unsigned char *b) {
    char raop[220], air[180], host[140];
    snprintf(raop, sizeof(raop), "%s@%s._raop._tcp.local", g_id, g_name);
    snprintf(air, sizeof(air), "%s._airplay._tcp.local", g_name);
    snprintf(host, sizeof(host), "%s.local", g_name);
    int p = 0;
    p = u16(b, p, 0);
    p = u16(b, p, 0x8400);
    p = u16(b, p, 0);
    p = u16(b, p, 6);
    p = u16(b, p, 0);
    p = u16(b, p, 0);
    p = ptr(b, p, "_raop._tcp.local", raop);
    p = srv(b, p, raop, g_port, host);
    p = txt_raop(b, p, raop);
    p = ptr(b, p, "_airplay._tcp.local", air);
    p = srv(b, p, air, g_port, host);
    p = txt_airplay(b, p, air);
    return p;
}

static void announce(void) {
    unsigned char packet[AIRTIZEN_MDNS_MAX];
    struct sockaddr_in to;
    int len = build_packet(packet);
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_port = htons(AIRTIZEN_MDNS_PORT);
    inet_pton(AF_INET, AIRTIZEN_MDNS_GROUP, &to.sin_addr);
    sendto(g_sock, packet, len, 0, (struct sockaddr *)&to, sizeof(to));
}

static void *loop(void *arg) {
    (void)arg;
    struct sockaddr_in bind_addr;
    struct ip_mreq mreq;
    int yes = 1;
    g_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_sock < 0) { g_running = 0; return NULL; }
    setsockopt(g_sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(AIRTIZEN_MDNS_PORT);
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    bind(g_sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr));
    memset(&mreq, 0, sizeof(mreq));
    inet_pton(AF_INET, AIRTIZEN_MDNS_GROUP, &mreq.imr_multiaddr);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    setsockopt(g_sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));
    while (g_running) {
        announce();
        sleep(3);
    }
    close(g_sock);
    g_sock = -1;
    return NULL;
}

int airtizen_mdns_start(const char *device_name, const char *device_id, unsigned short raop_port) {
    if (g_running) return 0;
    if (device_name && *device_name) snprintf(g_name, sizeof(g_name), "%s", device_name);
    if (device_id && *device_id) snprintf(g_id, sizeof(g_id), "%s", device_id);
    g_port = raop_port ? raop_port : 5000;
    g_running = 1;
    if (pthread_create(&g_thread, NULL, loop, NULL) != 0) {
        g_running = 0;
        return -1;
    }
    return 0;
}

void airtizen_mdns_stop(void) {
    if (!g_running) return;
    g_running = 0;
    if (g_sock >= 0) shutdown(g_sock, SHUT_RDWR);
    pthread_join(g_thread, NULL);
}

int airtizen_mdns_is_running(void) { return g_running; }
