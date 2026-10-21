/* Enforce a bounded allocation budget and correct capacity growth for
 * handshake responses, including headers longer than the initial buffer. */
#include "http_response.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *__real_malloc(size_t);
void *__real_realloc(void *, size_t);
static size_t peak;
static unsigned reallocations;
void *__wrap_malloc(size_t size) {
    if (size > peak) peak = size;
    assert(size <= 128 * 1024);
    return __real_malloc(size);
}
void *__wrap_realloc(void *pointer, size_t size) {
    assert(++reallocations <= 300);
    if (size > peak) peak = size;
    assert(size <= 128 * 1024);
    return __real_realloc(pointer, size);
}
int main(void) {
    char body[2048];
    int cycle, length;
    memset(body, 'x', sizeof(body));
    for (cycle = 0; cycle < 100; ++cycle) {
        const char *wire;
        http_response_t *response = http_response_init("RTSP/1.0", 200, "OK");
        assert(response);
        http_response_add_header(response, "CSeq", "1");
        http_response_add_header(response, "Server", "AirTunes/845.5.1");
        http_response_add_header(response, "Content-Type", "application/x-apple-binary-plist");
        http_response_finish(response, body, sizeof(body));
        wire = http_response_get_data(response, &length);
        assert(length > (int)sizeof(body) && length < 4096);
        assert(!memcmp(wire + length - sizeof(body), body, sizeof(body)));
        http_response_destroy(response);
    }
    assert(peak <= 4096);
    {
        http_response_t *response = http_response_init("HTTP/1.1", 200, "OK");
        char value[1001];
        const char *wire;
        memset(value, 'z', sizeof(value)-1); value[sizeof(value)-1] = 0;
        for (cycle = 0; cycle < 8; ++cycle) http_response_add_header(response, "X-Test", value);
        http_response_finish(response, NULL, 0);
        wire = http_response_get_data(response, &length);
        assert(length > 8000 && length < 16384);
        assert(!memcmp(wire + length - 21, "Content-Length: 0\r\n\r\n", 21));
        http_response_destroy(response);
    }
    printf("100 handshakes and long headers: %u reallocations; peak %lu bytes\n", reallocations, (unsigned long)peak);
    return 0;
}
