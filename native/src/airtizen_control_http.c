#include "airtizen_control_http.h"
#include "airtizen_runtime.h"
#include "airtizen_video.h"
#include "airtizen_playback.h"
#include "airtizen_photo.h"
#include "airtizen_metadata.h"
#include <math.h>
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

static pthread_t g_thread;
static int g_started, g_server_fd = -1, g_stop[2] = {-1, -1};
static unsigned short g_port;
/* nacl_io does not implement SO_RCVTIMEO/SO_SNDTIMEO. Bound I/O with select. */
static int wait_socket(int fd,int writing) {
    fd_set set;struct timeval timeout={1,0};int result;
    do {
        FD_ZERO(&set);FD_SET(fd,&set);timeout.tv_sec=1;timeout.tv_usec=0;
        result=select(fd+1,writing?NULL:&set,writing?&set:NULL,NULL,&timeout);
    } while(result<0 && errno==EINTR);
    return result>0?0:-1;
}
static int send_all(int fd, const char *data, size_t length) {
    while (length) {
        ssize_t n;
        if(wait_socket(fd,1))return -1;
        n = send(fd, data, length>16384?16384:length, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        data += n; length -= (size_t)n;
    }
    return 0;
}
static void respond_bytes(int fd,const char *status,const char *type,const void *body,size_t size,unsigned generation) {
    char header[1024];
    int n=snprintf(header,sizeof(header),
        "HTTP/1.1 %s\r\nContent-Type: %s\r\n"
        "Access-Control-Allow-Origin: null\r\nVary: Origin\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Access-Control-Allow-Headers: X-AirTizen-Control\r\n"
        "Access-Control-Expose-Headers: X-AirTizen-Generation\r\n"
        "X-AirTizen-Generation: %u\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n"
        "Connection: close\r\nContent-Length: %zu\r\n\r\n",status,type,generation,size);
    if(n>0 && (size_t)n<sizeof(header) && send_all(fd,header,(size_t)n)==0 && size)
        send_all(fd,body,size);
}
static void respond(int fd,const char *status,const char *body) {
    respond_bytes(fd,status,"application/json; charset=utf-8",body,strlen(body),0);
}
static void handle_client(int fd) {
    char req[4096], method[16], path[256], version[16], expected_host[64];
    char *end, *line, *next;
    size_t used = 0;
    int control = 0, host = 0, origin = 0, length_seen = 0;
    int playback_report=0;unsigned report_generation=0;double report_position=0,report_duration=0;int report_ready=0,report_failed=0,report_ended=0;
    int photo=0,photo_report=0,photo_failed=0;unsigned photo_generation=0;
    int artwork=0;unsigned artwork_version=0;
    int media_init=0,media_segment=0;uint64_t media_sequence=0;
    struct timeval timeout = {1, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    req[0] = 0;
    while (!(end = strstr(req, "\r\n\r\n"))) {
        ssize_t n;
        if (used == sizeof(req) - 1) { respond(fd, "431 Request Header Fields Too Large", "{\"ok\":false}"); return; }
        if(wait_socket(fd,0)){respond(fd,"408 Request Timeout","{\"ok\":false}");return;}
        n = recv(fd, req + used, sizeof(req) - 1 - used, 0);
        if (n <= 0) { respond(fd, "408 Request Timeout", "{\"ok\":false}"); return; }
        if (memchr(req + used, 0, (size_t)n)) { respond(fd, "400 Bad Request", "{\"ok\":false}"); return; }
        used += (size_t)n; req[used] = 0;
    }
    line = strstr(req, "\r\n");
    *line = 0;
    {
        char extra;
        if (sscanf(req, "%15s %255s %15s %c", method, path, version, &extra) != 3 ||
            (strcmp(version, "HTTP/1.1") && strcmp(version, "HTTP/1.0"))) {
            respond(fd, "400 Bad Request", "{\"ok\":false}"); return;
        }
    }
    if(!strncmp(path,"/playback/report?",17)) {
        int parsed=0;
        playback_report=sscanf(path,"/playback/report?generation=%u&position=%lf&duration=%lf&ready=%d&failed=%d%n",&report_generation,&report_position,&report_duration,&report_ready,&report_failed,&parsed)==5;
        if(playback_report && path[parsed]) {
            int tail=0;playback_report=sscanf(path+parsed,"&ended=%d%n",&report_ended,&tail)==1 && !path[parsed+tail];
        }
        playback_report=playback_report && (report_ready==0 || report_ready==1) && (report_failed==0 || report_failed==1) && (report_ended==0 || report_ended==1);
    }
    { int parsed=0;artwork=sscanf(path,"/artwork/%u%n",&artwork_version,&parsed)==1 && !path[parsed]; }
    {int parsed=0;photo=sscanf(path,"/photo/%u%n",&photo_generation,&parsed)==1 && !path[parsed];}
    {int parsed=0;photo_report=sscanf(path,"/photo/report?generation=%u&failed=%d%n",&photo_generation,&photo_failed,&parsed)==2 && !path[parsed] && (photo_failed==0 || photo_failed==1);}
    media_init=!strcmp(path,"/video/init");
    if(!strncmp(path,"/video/segment/",15)) {
        const char *digit=path+15;char *tail;
        while(*digit>='0' && *digit<='9')++digit;
        if(digit!=path+15 && !*digit) {
            errno=0;media_sequence=strtoull(path+15,&tail,10);media_segment=errno==0 && !*tail;
        }
    }
    snprintf(expected_host, sizeof(expected_host), "127.0.0.1:%u", g_port);
    for (line += 2; line < end; line = next + 2) {
        char *colon, *value;
        next = strstr(line, "\r\n");
        if (!next) break;
        *next = 0; colon = strchr(line, ':');
        if (!colon) { respond(fd, "400 Bad Request", "{\"ok\":false}"); return; }
        *colon = 0; value = colon + 1;
        while (*value == ' ' || *value == '\t') ++value;
        if (!strcasecmp(line, "Host")) {
            if (host || strcmp(value, expected_host)) { respond(fd, "403 Forbidden", "{\"ok\":false,\"error\":\"invalid_host\"}"); return; }
            host = 1;
        } else if (!strcasecmp(line, "Origin")) {
            if (origin || strcmp(value, "null")) { respond(fd, "403 Forbidden", "{\"ok\":false,\"error\":\"invalid_origin\"}"); return; }
            origin = 1;
        } else if (!strcasecmp(line, "X-AirTizen-Control")) control = !strcmp(value, "1");
        else if (!strcasecmp(line, "Content-Length")) {
            if (length_seen || strcmp(value, "0")) { respond(fd, "413 Content Too Large", "{\"ok\":false}"); return; }
            length_seen = 1;
        } else if (!strcasecmp(line, "Transfer-Encoding")) { respond(fd, "400 Bad Request", "{\"ok\":false}"); return; }
    }
    if (!host) { respond(fd, "400 Bad Request", "{\"ok\":false,\"error\":\"missing_host\"}"); return; }
    if (!photo && !photo_report && !artwork && !playback_report && !media_init && !media_segment && strcmp(path, "/status") && strcmp(path, "/start") && strcmp(path, "/stop")) {
        respond(fd, "404 Not Found", "{\"ok\":false,\"error\":\"not_found\"}"); return;
    }
    if (!strcmp(method, "OPTIONS")) { respond(fd, "204 No Content", ""); return; }
    if(!strcmp(method,"GET") && photo){
        uint8_t *body=NULL;char mime[16];size_t size=airtizen_photo_copy(photo_generation,&body,mime);
        if(size)respond_bytes(fd,"200 OK",mime,body,size,photo_generation);else respond(fd,"404 Not Found","{\"ok\":false}");
        free(body);return;
    }
    if(!strcmp(method,"POST") && photo_report){
        if(!control){respond(fd,"403 Forbidden","{\"ok\":false}");return;}
        if(airtizen_photo_report(photo_generation,photo_failed)==0)respond(fd,"200 OK","{\"ok\":true}");else respond(fd,"409 Conflict","{\"ok\":false}");
        return;
    }
    if(!strcmp(method,"GET") && artwork){
        uint8_t *body=NULL;char mime[16];size_t size=airtizen_metadata_copy_artwork(artwork_version,&body,mime);
        if(size)respond_bytes(fd,"200 OK",mime,body,size,artwork_version);
        else respond(fd,"404 Not Found","{\"ok\":false}");
        free(body);return;
    }
    if(!strcmp(method,"POST") && playback_report) {
        if(!control){respond(fd,"403 Forbidden","{\"ok\":false}");return;}
        if(airtizen_playback_report(report_generation,report_position,report_duration,report_ready,report_failed,report_ended)==0)
            respond(fd,"200 OK","{\"ok\":true}");
        else respond(fd,"409 Conflict","{\"ok\":false}");
        return;
    }
    if(!strcmp(method,"GET") && (media_init || media_segment)) {
        uint8_t *body=NULL;unsigned generation=0;
        size_t size=airtizen_video_copy(media_init,media_sequence,&body,&generation);
        if(size)respond_bytes(fd,"200 OK","video/mp4",body,size,generation);
        else respond(fd,"410 Gone","{\"ok\":false,\"error\":\"media_not_available\"}");
        free(body);return;
    }
    if (!strcmp(method, "GET") && !strcmp(path, "/status")) {
        respond(fd, "200 OK", airtizen_runtime_status_json()); return;
    }
    if (!strcmp(method, "POST") && (!strcmp(path, "/start") || !strcmp(path, "/stop"))) {
        int rc = 0;
        /* Reject simple cross-origin forms. Opaque null origins are not authentication;
         * a trusted app/service authorization channel is still a production requirement. */
        if (!control) { respond(fd, "403 Forbidden", "{\"ok\":false,\"error\":\"control_header_required\"}"); return; }
        if (!strcmp(path, "/start")) rc = airtizen_runtime_start(NULL);
        else airtizen_runtime_stop();
        respond(fd, rc == 0 ? "200 OK" : "503 Service Unavailable", airtizen_runtime_status_json()); return;
    }
    respond(fd, "405 Method Not Allowed", "{\"ok\":false,\"error\":\"method_not_allowed\"}");
}
static void *server_thread(void *arg) {
    (void)arg;
    for (;;) {
        fd_set fds;
        int maximum = g_server_fd > g_stop[0] ? g_server_fd : g_stop[0];
        int rc;
        FD_ZERO(&fds); FD_SET(g_server_fd, &fds); FD_SET(g_stop[0], &fds);
        rc = select(maximum + 1, &fds, NULL, NULL, NULL);
        if (rc < 0) { if (errno == EINTR) continue; break; }
        if (FD_ISSET(g_stop[0], &fds)) break;
        if (FD_ISSET(g_server_fd, &fds)) {
            int fd = accept(g_server_fd, NULL, NULL);
            if (fd < 0) continue;
            handle_client(fd); close(fd);
        }
    }
    return NULL;
}
int airtizen_control_http_start(unsigned short port) {
    struct sockaddr_in addr;
    int on = 1;
    if (g_started) return 0;
    g_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_server_fd < 0) return -1;
    setsockopt(g_server_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET; addr.sin_port = htons(port); addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(g_server_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(g_server_fd, 8) != 0 || pipe(g_stop) != 0) goto failed;
    g_port = port;
    if (pthread_create(&g_thread, NULL, server_thread, NULL) != 0) goto failed;
    g_started = 1; return 0;
failed:
    close(g_server_fd); g_server_fd = -1;
    if (g_stop[0] >= 0) close(g_stop[0]);
    if (g_stop[1] >= 0) close(g_stop[1]);
    g_stop[0] = g_stop[1] = -1;
    return -2;
}
void airtizen_control_http_stop(void) {
    char stop = 1;
    if (!g_started) return;
    (void)write(g_stop[1], &stop, 1); pthread_join(g_thread, NULL);
    close(g_server_fd); close(g_stop[0]); close(g_stop[1]);
    g_server_fd = g_stop[0] = g_stop[1] = -1; g_started = 0;
}
