/* Run real discovery startup against PPAPI's stricter socket lifecycle.
 * Keep real UDP descriptors, pipes and worker threads. Redirect bind to an
 * ephemeral port and capture outgoing DNS packets so no LAN traffic escapes. */
#include "airtizen_mdns.h"
#include "airtizen_capabilities.h"
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>

int __real_socket(int, int, int);
int __real_bind(int, const struct sockaddr *, socklen_t);

static int udp_fd, bound, reused, ttl, joined, fail_ttl, fail_join;
static unsigned announcements, goodbyes;
static int expected_uxplay;
static pthread_mutex_t capture = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t sent = PTHREAD_COND_INITIALIZER;

int __wrap_socket(int family, int type, int protocol) {
    assert(family == AF_INET && type == SOCK_DGRAM);
    bound = reused = ttl = joined = 0;
    udp_fd = __real_socket(family, type, protocol);
    return udp_fd;
}

int __wrap_setsockopt(int fd, int level, int option, const void *value, socklen_t size) {
    assert(fd == udp_fd);
    if (level == SOL_SOCKET && option == SO_REUSEADDR) {
        assert(!bound && size == sizeof(int) && *(const int *)value == 1);
        reused = 1; return 0;
    }
    assert(level == IPPROTO_IP);
    if (option == IP_MULTICAST_TTL) {
        /* ppb_udp_socket.h: multicast TTL can ONLY be set before Bind().
         * Pepper maps PP_ERROR_FAILED to EPERM, matching the reported TV. */
        if (bound || fail_ttl) { errno = EPERM; return -1; }
        assert(size == sizeof(unsigned int));
        ttl = *(const unsigned int *)value;
        assert(ttl == 255); return 0;
    }
    assert(bound);
    if (option == IP_ADD_MEMBERSHIP) {
        const struct ip_mreq *membership = value;
        assert(size == sizeof(*membership));
        assert(ntohl(membership->imr_multiaddr.s_addr) == 0xe00000fbU);
        assert(ntohl(membership->imr_interface.s_addr) == 0xc0000214U);
        if (fail_join) { errno = EACCES; return -1; }
        joined = 1; return 0;
    }
    assert(option == IP_MULTICAST_IF && joined);
    assert(size == sizeof(struct in_addr));
    assert(ntohl(((const struct in_addr *)value)->s_addr) == 0xc0000214U);
    return 0;
}

int __wrap_bind(int fd, const struct sockaddr *address, socklen_t size) {
    struct sockaddr_in redirected;
    assert(fd == udp_fd && reused && size == sizeof(redirected));
    memcpy(&redirected, address, size);
    assert(ntohs(redirected.sin_port) == 5353);
    redirected.sin_port = 0;
    if (__real_bind(fd, (const struct sockaddr *)&redirected, size)) return -1;
    bound = 1; return 0;
}

static uint32_t wire32(const unsigned char *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static int has_txt(const unsigned char *payload, size_t size, const char *value) {
    size_t position = 0, wanted = strlen(value);
    while (position < size) {
        unsigned length = payload[position++];
        assert(position + length <= size);
        if (length == wanted && !memcmp(payload + position, value, wanted)) return 1;
        position += length;
    }
    return 0;
}

ssize_t __wrap_sendto(int fd, const void *body, size_t size, int flags,
                      const struct sockaddr *address, socklen_t length) {
    const unsigned char *packet = body;
    const struct sockaddr_in *destination = (const struct sockaddr_in *)address;
    size_t position = 12;
    unsigned i;
    uint32_t record_ttl = 0;
    (void)flags;
    assert(fd == udp_fd && bound && joined && ttl == 255);
    assert(length == sizeof(*destination) && ntohs(destination->sin_port) == 5353);
    assert(ntohl(destination->sin_addr.s_addr) == 0xe00000fbU);
    assert(size > 12 && size <= 1400 && packet[2] == 0x84 && packet[7] == 7);
    for (i = 0; i < 7; ++i) {
        while (position < size && packet[position]) {
            assert(packet[position] <= 63);
            position += 1 + packet[position];
        }
        assert(position + 11 <= size);
        ++position;
        if (!i) record_ttl = wire32(packet + position + 4);
        assert(wire32(packet + position + 4) == record_ttl);
        if (i == 2 || i == 5) {
            unsigned data_size = ((unsigned)packet[position + 8] << 8) + packet[position + 9];
            const unsigned char *payload = packet + position + 10;
            assert(position + 10 + data_size <= size);
            assert(has_txt(payload, data_size, "pk=5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a5a"));
            if (i == 2) {
                assert(has_txt(payload, data_size, expected_uxplay ? "ft=0x5A7FFEE6,0x0" : "ft=0x4803a383,0x0"));
                assert(has_txt(payload, data_size, expected_uxplay ? "am=AppleTV3,2" : "am=AirTizen1,1"));
                assert(has_txt(payload, data_size, expected_uxplay ? "cn=0,1,2,3" : "cn=1,2,3"));
                assert(has_txt(payload, data_size, "rhd=5.6.0.0") == expected_uxplay);
                assert(has_txt(payload, data_size, "sv=false") == expected_uxplay);
            } else {
                assert(has_txt(payload, data_size, expected_uxplay ? "features=0x5A7FFEE6,0x0" : "features=0x4803a383,0x0"));
                assert(has_txt(payload, data_size, expected_uxplay ? "model=AppleTV3,2" : "model=AirTizen1,1"));
            }
        }
        position += 10 + ((unsigned)packet[position + 8] << 8) + packet[position + 9];
        assert(position <= size);
    }
    assert(position == size && (record_ttl == 120 || record_ttl == 0));
    pthread_mutex_lock(&capture);
    if (record_ttl) ++announcements; else ++goodbyes;
    pthread_cond_signal(&sent);
    pthread_mutex_unlock(&capture);
    return (ssize_t)size;
}

static void expect_closed(void) {
    assert(!airtizen_mdns_is_running());
    errno = 0;
    assert(fcntl(udp_fd, F_GETFD) == -1 && errno == EBADF);
}

int main(void) {
    unsigned cycle;
    unsigned char key[32];
    memset(key, 0x5a, sizeof(key));
    assert(airtizen_discovery_set_profile("invalid") == -1);
    assert(airtizen_discovery_features() == AIRTIZEN_FEATURES);
    assert(airtizen_mdns_set_network("192.0.2.20", "020000000001") == 0);
    assert(airtizen_mdns_set_public_key(key) == 0);
    fail_ttl = 1;
    assert(airtizen_mdns_start("Sandbox TV", NULL, 5000) < 0);
    assert(!strcmp(airtizen_mdns_last_error(), "mdns_ttl_failed_errno_1"));
    expect_closed();
    fail_ttl = 0; fail_join = 1;
    assert(airtizen_mdns_start("Sandbox TV", NULL, 5000) < 0);
    assert(!strncmp(airtizen_mdns_last_error(), "mdns_join_group_failed_errno_", 29));
    expect_closed();
    assert(!announcements && !goodbyes);
    fail_join = 0;
    for (cycle = 0; cycle < 2; ++cycle) {
        struct timespec deadline;
        const char *receiver = cycle ? "123456789012345678901234567890123456789012345678" : "Sandbox TV";
        int result;
        expected_uxplay = (int)cycle;
        assert(!airtizen_discovery_set_profile(cycle ? "uxplay-test" : "native"));
        result = airtizen_mdns_start(receiver, NULL, 5000);
        if (result) fprintf(stderr, "Discovery startup rejected: %s\n", airtizen_mdns_last_error());
        assert(result == 0);
        assert(airtizen_mdns_is_running() && !airtizen_mdns_last_error()[0]);
        assert(airtizen_discovery_set_profile(cycle ? "native" : "uxplay-test") == -1);
        clock_gettime(CLOCK_REALTIME, &deadline); deadline.tv_sec += 3;
        pthread_mutex_lock(&capture);
        while (announcements <= cycle) assert(pthread_cond_timedwait(&sent, &capture, &deadline) == 0);
        pthread_mutex_unlock(&capture);
        airtizen_mdns_stop();
        expect_closed();
        assert(goodbyes == cycle + 1);
    }
    puts("PPAPI lifecycle: pre-bind TTL255, membership, DNS records, goodbye, restart and fatal errors verified");
    return 0;
}
