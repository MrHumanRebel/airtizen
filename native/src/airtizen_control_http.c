#include "airtizen_control_http.h"
#include "airtizen_runtime.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>
#else
#include <winsock2.h>
#endif

#ifndef _WIN32
static pthread_t g_thread;
static int g_thread_started = 0;
#endif
static int g_server_fd = -1;
static volatile int g_running = 0;

static void send_resp(int fd, const char *status, const char *body)
{
    char buf[8192];
    int n = snprintf(buf, sizeof(buf),
                     "HTTP/1.1 %s\r\n"
                     "Content-Type: application/json\r\n"
                     "Access-Control-Allow-Origin: *\r\n"
                     "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                     "Access-Control-Allow-Headers: Content-Type\r\n"
                     "Connection: close\r\n"
                     "Content-Length: %lu\r\n\r\n%s",
                     status,
                     (unsigned long)strlen(body),
                     body);
    if (n > 0) {
        send(fd, buf, (size_t)n, 0);
    }
}

static void handle_client(int fd)
{
    char req[2048];
    int n = recv(fd, req, sizeof(req) - 1, 0);
    if (n <= 0) {
        return;
    }
    req[n] = '\0';

    if (strncmp(req, "OPTIONS ", 8) == 0) {
        send_resp(fd, "204 No Content", "{}");
    } else if (strncmp(req, "GET /status", 11) == 0) {
        send_resp(fd, "200 OK", airtizen_runtime_status_json());
    } else if (strncmp(req, "POST /start", 11) == 0) {
        int rc = airtizen_runtime_start("AirTizen TV");
        if (rc == 0) {
            send_resp(fd, "200 OK", airtizen_runtime_status_json());
        } else {
            send_resp(fd, "500 Internal Server Error", airtizen_runtime_status_json());
        }
    } else if (strncmp(req, "POST /stop", 10) == 0) {
        airtizen_runtime_stop();
        send_resp(fd, "200 OK", airtizen_runtime_status_json());
    } else {
        send_resp(fd, "404 Not Found", "{\"ok\":false,\"error\":\"not_found\"}");
    }
}

#ifndef _WIN32
static void *server_thread(void *arg)
{
    (void)arg;
    while (g_running) {
        int cfd;
        struct sockaddr_in caddr;
        socklen_t clen = (socklen_t)sizeof(caddr);
        cfd = accept(g_server_fd, (struct sockaddr *)&caddr, &clen);
        if (cfd < 0) {
            if (!g_running) {
                break;
            }
            continue;
        }
        handle_client(cfd);
        close(cfd);
    }
    return NULL;
}
#endif

int airtizen_control_http_start(unsigned short port)
{
#ifndef _WIN32
    struct sockaddr_in addr;
    int on = 1;
    if (g_running) {
        return 0;
    }

    g_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_server_fd < 0) {
        return -1;
    }
    setsockopt(g_server_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(g_server_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(g_server_fd);
        g_server_fd = -1;
        return -2;
    }
    if (listen(g_server_fd, 8) != 0) {
        close(g_server_fd);
        g_server_fd = -1;
        return -3;
    }

    g_running = 1;
    if (pthread_create(&g_thread, NULL, server_thread, NULL) != 0) {
        g_running = 0;
        close(g_server_fd);
        g_server_fd = -1;
        return -4;
    }
    g_thread_started = 1;
    return 0;
#else
    (void)port;
    return 0;
#endif
}

void airtizen_control_http_stop(void)
{
#ifndef _WIN32
    if (!g_running) {
        return;
    }
    g_running = 0;
    if (g_server_fd >= 0) {
        shutdown(g_server_fd, SHUT_RDWR);
        close(g_server_fd);
        g_server_fd = -1;
    }
    if (g_thread_started) {
        pthread_join(g_thread, NULL);
        g_thread_started = 0;
    }
#endif
}
