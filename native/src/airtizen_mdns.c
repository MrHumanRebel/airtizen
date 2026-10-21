#include "airtizen_mdns.h"
#include "airtizen_capabilities.h"

#include <arpa/inet.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#ifndef __native_client__
#include <ifaddrs.h>
#include <net/if.h>
#include <sys/ioctl.h>
#endif
#include <sys/select.h>

#define AIRTIZEN_MDNS_GROUP "224.0.0.251"
#define AIRTIZEN_MDNS_PORT 5353
#define AIRTIZEN_MDNS_MAX 1400

static pthread_t g_thread;
static pthread_mutex_t g_identity_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_running = 0;
static int g_stop[2] = {-1, -1};
static unsigned int g_ttl = 120;
static int g_sock = -1;
static char g_name[96] = "AirTizen TV";
static char g_id[32] = "A1B2C3D4E5F6";
static char g_public_key[65];
static unsigned short g_port = 5000;
static struct in_addr g_address;
static int g_network_override;
static char g_start_error[80];
const char *airtizen_mdns_last_error(void) { return g_start_error; }
static void start_error(const char *stage, int error) {
    snprintf(g_start_error, sizeof(g_start_error), "mdns_%s_failed_errno_%d", stage, error);
}

int airtizen_mdns_set_public_key(const unsigned char key[32]) {
    size_t i;
    if (!key) return -1;
    pthread_mutex_lock(&g_identity_lock);
    if (g_running) { pthread_mutex_unlock(&g_identity_lock); return -1; }
    for (i = 0; i < 32; ++i) snprintf(g_public_key + 2*i, 3, "%02x", key[i]);
    pthread_mutex_unlock(&g_identity_lock);
    return 0;
}

void airtizen_mdns_pairing_id(char uuid[37]) {
    pthread_mutex_lock(&g_identity_lock);
    snprintf(uuid, 37, "00000000-0000-4000-8000-%.12s", g_id);
    pthread_mutex_unlock(&g_identity_lock);
}

int airtizen_mdns_set_network(const char *ipv4,const char *id) {
    struct in_addr address;size_t i;uint32_t host;
    if(!ipv4 || !id || strlen(id)!=12 || inet_pton(AF_INET,ipv4,&address)!=1)return -1;
    host=ntohl(address.s_addr);
    if(!host || (host>>24)==127 || (host>>24)>=224 || host==0xffffffffU)return -1;
    for(i=0;i<12;++i)if(!((id[i]>='0' && id[i]<='9') || (id[i]>='A' && id[i]<='F')))return -1;
    pthread_mutex_lock(&g_identity_lock);
    if(g_running){pthread_mutex_unlock(&g_identity_lock);return -1;}
    g_address=address;memcpy(g_id,id,13);g_network_override=1;
    pthread_mutex_unlock(&g_identity_lock);return 0;
}

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
    p = u32(b, p, g_ttl);
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
    p = u16(b, p, 0x8001);
    p = u32(b, p, g_ttl);
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

static int txt_raop_data(unsigned char *b, int p) {
    char key[68];
    p = txt(b, p, "txtvers=1");
    p = txt(b, p, "ch=2");
    p = txt(b, p, "md=0,1,2");
    p = txt(b, p, "cn=1,2,3");
    p = txt(b, p, "da=true");
    p = txt(b, p, "et=0,3,5");
    p = txt(b, p, "pw=false");
    p = txt(b, p, "sr=44100");
    p = txt(b, p, "ss=16");
    p = txt(b, p, "tp=UDP");
    p = txt(b, p, "vn=65537");
    p = txt(b, p, "vs=220.68");
    p = txt(b, p, "sf=0x4");
    if (g_public_key[0]) { snprintf(key, sizeof(key), "pk=%s", g_public_key); p = txt(b, p, key); }
    return p;
}

static int txt_airplay_data(unsigned char *b, int p) {
    char key[68], identifier[40];
    char deviceid[40];
    snprintf(deviceid, sizeof(deviceid), "deviceid=%.2s:%.2s:%.2s:%.2s:%.2s:%.2s", g_id, g_id+2, g_id+4, g_id+6, g_id+8, g_id+10);
    p = txt(b, p, deviceid);
    p = txt(b, p, AIRTIZEN_FEATURES_TXT);
    p = txt(b, p, "flags=0x4");
    p = txt(b, p, "model=AirTizen1,1");
    p = txt(b, p, "srcvers=220.68");
    p = txt(b, p, "vv=2");
    if (g_public_key[0]) { snprintf(key, sizeof(key), "pk=%s", g_public_key); p = txt(b, p, key); }
    snprintf(identifier, sizeof(identifier), "pi=00000000-0000-4000-8000-%.12s", g_id);
    p = txt(b, p, identifier);
    return p;
}

size_t airtizen_mdns_txt(int airplay, unsigned char *out, size_t capacity) {
    unsigned char data[512]; int size;
    pthread_mutex_lock(&g_identity_lock);
    size = airplay ? txt_airplay_data(data, 0) : txt_raop_data(data, 0);
    pthread_mutex_unlock(&g_identity_lock);
    if (!out || capacity < (size_t)size) return 0;
    memcpy(out, data, (size_t)size); return (size_t)size;
}

static int txt_record(unsigned char *b, int p, const char *instance, int airplay) {
    int length, start;
    p = name(b, p, instance); p = u16(b, p, 16); p = u16(b, p, 0x8001);
    p = u32(b, p, g_ttl); length = p; p = u16(b, p, 0); start = p;
    p = airplay ? txt_airplay_data(b, p) : txt_raop_data(b, p);
    b[length] = (unsigned char)((p - start) >> 8); b[length + 1] = (unsigned char)(p - start);
    return p;
}

static int address_record(unsigned char *b, int p, const char *host) {
    p = name(b, p, host);
    p = u16(b, p, 1); /* A */
    p = u16(b, p, 0x8001);
    p = u32(b, p, g_ttl);
    p = u16(b, p, 4);
    memcpy(b + p, &g_address.s_addr, 4);
    return p + 4;
}

static int discover_local_address(void) {
    if(g_network_override)return 0;
#ifdef __native_client__
    return -1; /* No Linux interface ioctls inside the NaCl sandbox. */
#else
    struct ifaddrs *all, *it;
    int found = 0;
    if (getifaddrs(&all) != 0) return -1;
    for (it = all; it; it = it->ifa_next) {
        struct ifreq req;
        int fd;
        unsigned char *mac;
        if (!it->ifa_addr || it->ifa_addr->sa_family != AF_INET ||
            !(it->ifa_flags & IFF_UP) || (it->ifa_flags & IFF_LOOPBACK)) continue;
        g_address = ((struct sockaddr_in *)it->ifa_addr)->sin_addr;
        memset(&req, 0, sizeof(req));
        snprintf(req.ifr_name, sizeof(req.ifr_name), "%s", it->ifa_name);
        fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd < 0) continue;
        if (ioctl(fd, SIOCGIFHWADDR, &req) == 0) {
            mac = (unsigned char *)req.ifr_hwaddr.sa_data;
            snprintf(g_id, sizeof(g_id), "%02X%02X%02X%02X%02X%02X",
                     mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
            found = 1;
        }
        close(fd);
        if (found) break;
    }
    freeifaddrs(all);
    return found ? 0 : -1;
#endif
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
    p = u16(b, p, 7);
    p = u16(b, p, 0);
    p = u16(b, p, 0);
    p = ptr(b, p, "_raop._tcp.local", raop);
    p = srv(b, p, raop, g_port, host);
    p = txt_record(b, p, raop, 0);
    p = ptr(b, p, "_airplay._tcp.local", air);
    p = srv(b, p, air, g_port, host);
    p = txt_record(b, p, air, 1);
    p = address_record(b, p, host);
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

static int read_dns_name(const unsigned char *packet, size_t size, size_t *position, char *out, size_t capacity) {
    size_t cursor = *position, used = 0, resume = 0;
    unsigned jumps = 0;
    while (cursor < size) {
        unsigned label = packet[cursor++];
        if (!label) { *position = resume ? resume : cursor; out[used] = 0; return 0; }
        if ((label & 0xc0) == 0xc0) {
            if (cursor >= size || ++jumps > 8) return -1;
            if (!resume) resume = cursor + 1;
            cursor = ((size_t)(label & 0x3f) << 8) | packet[cursor];
            continue;
        }
        if (label > 63 || cursor + label > size || used + label + 2 > capacity) return -1;
        if (used) out[used++] = '.';
        memcpy(out + used, packet + cursor, label); used += label; cursor += label;
    }
    return -1;
}
static int query_matches(const unsigned char *packet, size_t size) {
    size_t position = 12;
    unsigned count, i;
    char receiver[256], host[160], raop[220], air[180];
    if (size < 12 || (packet[2] & 0xf8)) return 0;
    count = ((unsigned)packet[4] << 8) | packet[5];
    if (!count || count > 16) return 0;
    snprintf(host, sizeof(host), "%s.local", g_name);
    snprintf(raop, sizeof(raop), "%s@%s._raop._tcp.local", g_id, g_name);
    snprintf(air, sizeof(air), "%s._airplay._tcp.local", g_name);
    for (i = 0; i < count; ++i) {
        if (read_dns_name(packet, size, &position, receiver, sizeof(receiver)) != 0 || position + 4 > size) return 0;
        position += 4;
        if (!strcasecmp(receiver, "_raop._tcp.local") || !strcasecmp(receiver, "_airplay._tcp.local") ||
            !strcasecmp(receiver, "_services._dns-sd._udp.local") || !strcasecmp(receiver, host) ||
            !strcasecmp(receiver, raop) || !strcasecmp(receiver, air)) return 1;
    }
    return 0;
}

static void *loop(void *arg) {
    (void)arg;
    announce();
    for (;;) {
        fd_set fds;
        struct timeval timeout = {3, 0};
        int rc, maximum = g_sock > g_stop[0] ? g_sock : g_stop[0];
        FD_ZERO(&fds); FD_SET(g_sock, &fds); FD_SET(g_stop[0], &fds);
        rc = select(maximum + 1, &fds, NULL, NULL, &timeout);
        if (rc < 0) { if (errno == EINTR) continue; break; }
        if (FD_ISSET(g_stop[0], &fds)) break;
        if (rc == 0) announce();
        else if (FD_ISSET(g_sock, &fds)) {
            unsigned char query[AIRTIZEN_MDNS_MAX];
            ssize_t n = recv(g_sock, query, sizeof(query), 0);
            /* Respond to DNS questions, never to other responders' announcements. */
            if (n >= 12 && query_matches(query, (size_t)n)) announce();
        }
    }
    g_ttl = 0; announce(); g_ttl = 120;
    return NULL;
}

int airtizen_mdns_start(const char *device_name, const char *device_id, unsigned short raop_port) {
    struct sockaddr_in addr;
    struct ip_mreq membership;
    int yes = 1;
    unsigned int ttl = 255;
    if (g_running) return 0;
    g_start_error[0] = 0;
    if (!device_name || !*device_name || strlen(device_name) > 48) return -1;
    if (device_id) {
        size_t i;
        if (strlen(device_id) != 12) return -1;
        for (i = 0; i < 12; ++i)
            if (!((device_id[i] >= '0' && device_id[i] <= '9') ||
                  (device_id[i] >= 'A' && device_id[i] <= 'F'))) return -1;
    }
    pthread_mutex_lock(&g_identity_lock);
    if (discover_local_address() != 0) { start_error("network", errno); pthread_mutex_unlock(&g_identity_lock); return -1; }
    if (device_id) snprintf(g_id, sizeof(g_id), "%s", device_id);
    snprintf(g_name, sizeof(g_name), "%s", device_name);
    pthread_mutex_unlock(&g_identity_lock);
    g_port = raop_port ? raop_port : 5000;
    g_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_sock < 0) { start_error("udp_socket", errno); return -1; }
    if (setsockopt(g_sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) != 0) { start_error("reuseaddr", errno); goto failed; }
    /* PPAPI UDP 1.2 requires multicast TTL to be set before Bind(). Linux
     * allows the opposite order, which hid this error in host socket tests. */
    if (setsockopt(g_sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl)) != 0) {
        start_error("ttl", errno); goto failed;
    }
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET; addr.sin_port = htons(AIRTIZEN_MDNS_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(g_sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) { start_error("bind_5353", errno); goto failed; }
    memset(&membership, 0, sizeof(membership));
    inet_pton(AF_INET, AIRTIZEN_MDNS_GROUP, &membership.imr_multiaddr);
    membership.imr_interface = g_address;
    if (setsockopt(g_sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &membership, sizeof(membership)) != 0) {
        start_error("join_group", errno); goto failed;
    }
    if (setsockopt(g_sock, IPPROTO_IP, IP_MULTICAST_IF, &g_address, sizeof(g_address)) != 0) {
        start_error("interface", errno); goto failed;
    }
    if (pipe(g_stop) != 0) { start_error("stop_pipe", errno); goto failed; }
    { int error = pthread_create(&g_thread, NULL, loop, NULL);
      if (error != 0) { start_error("worker", error); goto failed; } }
    g_running = 1;
    return 0;
failed:
    close(g_sock); g_sock = -1;
    if (g_stop[0] >= 0) close(g_stop[0]);
    if (g_stop[1] >= 0) close(g_stop[1]);
    g_stop[0] = g_stop[1] = -1;
    return -1;
}

void airtizen_mdns_stop(void) {
    char stop = 1;
    if (!g_running) return;
    (void)write(g_stop[1], &stop, 1);
    pthread_join(g_thread, NULL);
    close(g_sock); close(g_stop[0]); close(g_stop[1]);
    g_sock = g_stop[0] = g_stop[1] = -1;
    g_running = 0;
}
int airtizen_mdns_is_running(void) { return g_running; }

void airtizen_mdns_identity(char *receiver, size_t receiver_size, char *id, size_t id_size) {
    pthread_mutex_lock(&g_identity_lock);
    snprintf(receiver, receiver_size, "%s", g_name);
    snprintf(id, id_size, "%.2s:%.2s:%.2s:%.2s:%.2s:%.2s", g_id, g_id+2, g_id+4, g_id+6, g_id+8, g_id+10);
    pthread_mutex_unlock(&g_identity_lock);
}
