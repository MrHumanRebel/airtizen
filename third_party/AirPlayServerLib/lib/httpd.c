/**
 *  Copyright (C) 2011-2012  Juho Vähä-Herttua
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 */

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>


#include "httpd.h"
#include "netutils.h"
#include "http_request.h"
#include "compat.h"
#include "logger.h"
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

struct http_connection_s {
	int connected;

	int socket_fd;
	void *user_data;
	http_request_t *request;
    unsigned long received_bytes;
    unsigned request_count;
};
typedef struct http_connection_s http_connection_t;

struct httpd_s {
	logger_t *logger;
	httpd_callbacks_t callbacks;

	int max_connections;
	int open_connections;
	http_connection_t *connections;

	/* These variables only edited mutex locked */
	int running;
	int joined;
	thread_handle_t thread;
	mutex_handle_t run_mutex;

	/* Server fds for accepting connections */
	int server_fd4;
	int server_fd6;
};

httpd_t *
httpd_init(logger_t *logger, httpd_callbacks_t *callbacks, int max_connections)
{
	httpd_t *httpd;

	assert(logger);
	assert(callbacks);
	assert(max_connections > 0);

	/* Allocate the httpd_t structure */
	httpd = calloc(1, sizeof(httpd_t));
	if (!httpd) {
		return NULL;
	}

	httpd->max_connections = max_connections;
	httpd->connections = calloc(max_connections, sizeof(http_connection_t));
	if (!httpd->connections) {
		free(httpd);
		return NULL;
	}

	/* Use the logger provided */
	httpd->logger = logger;

	/* Save callback pointers */
	memcpy(&httpd->callbacks, callbacks, sizeof(httpd_callbacks_t));

	/* Initial status joined */
	httpd->running = 0;
	httpd->joined = 1;

	return httpd;
}

void
httpd_destroy(httpd_t *httpd)
{
	if (httpd) {
		httpd_stop(httpd);

		free(httpd->connections);
		free(httpd);
	}
}

static int
httpd_add_connection(httpd_t *httpd, int fd, unsigned char *local, int local_len, unsigned char *remote, int remote_len)
{
	void *user_data;
	int i;

	for (i=0; i<httpd->max_connections; i++) {
		if (!httpd->connections[i].connected) {
			break;
		}
	}
	if (i == httpd->max_connections) {
		/* This code should never be reached, we do not select server_fds when full */
		logger_log(httpd->logger, LOGGER_INFO, "Max connections reached");
		return -1;
	}

	user_data = httpd->callbacks.conn_init(httpd->callbacks.opaque, local, local_len, remote, remote_len);
	if (!user_data) {
		logger_log(httpd->logger, LOGGER_ERR, "Error initializing HTTP request handler");
		return -1;
	}

	httpd->open_connections++;
	httpd->connections[i].socket_fd = fd;
    httpd->connections[i].received_bytes = 0;
    httpd->connections[i].request_count = 0;
#ifndef _WIN32
    { struct timeval timeout={1,0};setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout)); }
#endif
	httpd->connections[i].connected = 1;
	httpd->connections[i].user_data = user_data;
	return 0;
}

static int
httpd_accept_connection(httpd_t *httpd, int server_fd, int is_ipv6)
{
	struct sockaddr_storage remote_saddr;
	socklen_t remote_saddrlen;
	struct sockaddr_storage local_saddr;
	socklen_t local_saddrlen;
	unsigned char *local, *remote;
	int local_len, remote_len;
	int ret, fd;

	remote_saddrlen = sizeof(remote_saddr);
	fd = accept(server_fd, (struct sockaddr *)&remote_saddr, &remote_saddrlen);
	if (fd == -1) {
		/* FIXME: Error happened */
		return -1;
	}

	local_saddrlen = sizeof(local_saddr);
	ret = getsockname(fd, (struct sockaddr *)&local_saddr, &local_saddrlen);
	if (ret == -1) {
		shutdown(fd, SHUT_RDWR);
		closesocket(fd);
		return 0;
	}

	logger_log(httpd->logger, LOGGER_INFO, "Accepted %s client on socket %d",
	           (is_ipv6 ? "IPv6"  : "IPv4"), fd);
	local = netutils_get_address(&local_saddr, &local_len);
	remote = netutils_get_address(&remote_saddr, &remote_len);

	ret = httpd_add_connection(httpd, fd, local, local_len, remote, remote_len);
	if (ret == -1) {
		shutdown(fd, SHUT_RDWR);
		closesocket(fd);
		return 0;
	}
	return 1;
}

static void
httpd_remove_connection(httpd_t *httpd, http_connection_t *connection)
{
	if (connection->request) {
		http_request_destroy(connection->request);
		connection->request = NULL;
	}
	httpd->callbacks.conn_destroy(connection->user_data);
	shutdown(connection->socket_fd, SHUT_WR);
	closesocket(connection->socket_fd);
	connection->connected = 0;
	httpd->open_connections--;
}

/* Extract only a known software product and bounded numeric version, excluding
 * appended device names, OS descriptions, identifiers and arbitrary strings. */
static int httpd_agent_product(const char *value, char product[48]) {
    static const char *prefixes[] = {"AirPlay/", "AirTunes/", "MediaControl/", "MediaRemote/", "iTunes/"};
    unsigned i;
    if (!value) return 0;
    for (i = 0; i < sizeof(prefixes)/sizeof(prefixes[0]); ++i) {
        size_t length = strlen(prefixes[i]), end = length;
        unsigned groups = 0;
        if (strncmp(value, prefixes[i], length)) continue;
        do {
            unsigned digits = 0;
            while (value[end] >= '0' && value[end] <= '9') { ++end; if (++digits > 4) return 0; }
            if (!digits || ++groups > 4) return 0;
            if (value[end] != '.') break;
            ++end;
        } while (1);
        if (value[end] && value[end] != ' ' && value[end] != '(' && value[end] != ';') return 0;
        if (end >= 48) return 0;
        memcpy(product, value, end); product[end] = 0; return 1;
    }
    return 0;
}

/* Emit canonical allowlisted field names and byte counts, not raw headers.
 * Unknown field names may themselves contain identifiers and are redacted. */
static void httpd_trace_options(httpd_t *httpd, http_connection_t *connection, http_request_t *request) {
    static const char *fields[] = {"CSeq", "User-Agent", "Apple-Challenge", "X-Apple-ProtocolVersion",
        "X-Apple-Session-ID", "X-Apple-Device-ID", "X-Apple-Device-Identifier", "X-Apple-Client-Name",
        "Client-Instance", "DACP-ID", "Active-Remote", "Authorization", "Content-Length",
        "Content-Type", "Connection", "Host", "Upgrade", "X-Apple-Purpose", "X-Apple-Session-Protocol-Version"};
    const char *url = http_request_get_url(request), *target;
    unsigned i, count = http_request_get_header_count(request);
    int body_size = 0;
    target = !strcmp(url, "*") ? "asterisk" : !strncmp(url, "rtsp://", 7) || !strncmp(url, "rtsps://", 8) ? "rtsp-uri" : url[0] == '/' ? "path" : "other";
    http_request_get_data(request, &body_size);
    logger_log(httpd->logger, LOGGER_INFO, "OPTIONS socket %d: protocol=%s target=%s headers=%u body=%d",
        connection->socket_fd, http_request_get_protocol(request), target, count, body_size);
    for (i = 0; i < count && i < 16; ++i) {
        const char *name = http_request_get_header_name(request, i), *label = "<other>", *value;
        unsigned j; char product[48];
        for (j = 0; j < sizeof(fields)/sizeof(fields[0]); ++j)
            if (name && !strcasecmp(name, fields[j])) { label = fields[j]; break; }
        value = http_request_get_header_value(request, i);
        if (!strcmp(label, "User-Agent") && httpd_agent_product(value, product))
            logger_log(httpd->logger, LOGGER_INFO, "OPTIONS header %s: %s bytes=%lu", label, product, (unsigned long)strlen(value));
        else logger_log(httpd->logger, LOGGER_INFO, "OPTIONS header %s: redacted bytes=%lu", label, value ? (unsigned long)strlen(value) : 0UL);
    }
    if (count > 16) logger_log(httpd->logger, LOGGER_INFO, "OPTIONS headers omitted=%u", count - 16);
}

/* Log only fixed route labels, never URL queries, raw headers or request bodies.
 * OPTIONS additionally records bounded, redacted header metadata above. */
static void httpd_trace_request(httpd_t *httpd, http_connection_t *connection,
                                http_request_t *request, http_response_t *response) {
    static const char *routes[] = {"/info", "/pair-setup", "/pair-verify", "/fp-setup",
        "/feedback", "/server-info", "/reverse", "/play", "/stop", "/photo", "/rate", "/scrub", "/playback-info"};
    const char *method = http_request_get_method(request), *url = http_request_get_url(request);
    const char *label = url && url[0] == '/' ? "<other>" : "<session>";
    unsigned i; int status = 0, length = 0;
    if (!method || !url) return; /* Reverse-channel response, not a request. */
    for (i = 0; i < sizeof(routes)/sizeof(routes[0]); ++i) {
        size_t n = strlen(routes[i]);
        if (!strncmp(url, routes[i], n) && (!url[n] || url[n] == '?')) { label = routes[i]; break; }
    }
    if (response) {
        const char *wire = http_response_get_data(response, &length);
        if (sscanf(wire, "%*15s %d", &status) != 1) status = 0;
    }
    ++connection->request_count;
    if (!strcmp(method, "OPTIONS")) httpd_trace_options(httpd, connection, request);
    logger_log(httpd->logger, LOGGER_INFO, "AirPlay socket %d: %s %s status=%d bytes=%d",
        connection->socket_fd, method, label, status, length);
}

static THREAD_RETVAL
httpd_thread(void *arg)
{
	httpd_t *httpd = arg;
	char buffer[1024];
	int i;

	assert(httpd);

	while (1) {
		fd_set rfds;
		struct timeval tv;
		int nfds=0;
		int ret;

		MUTEX_LOCK(httpd->run_mutex);
		if (!httpd->running) {
			MUTEX_UNLOCK(httpd->run_mutex);
			break;
		}
		MUTEX_UNLOCK(httpd->run_mutex);

		/* Set timeout value to 5ms */
		tv.tv_sec = 0;
		tv.tv_usec = 50000;

        if(httpd->callbacks.conn_poll)for(i=0;i<httpd->max_connections;++i) {
            http_connection_t *connection=&httpd->connections[i];http_response_t *event=NULL;int status;
            if(!connection->connected)continue;
            status=httpd->callbacks.conn_poll(connection->user_data,&event);
            if(status<0){httpd_remove_connection(httpd,connection);continue;}
            if(event){
                int size,written=0;const char *data=http_response_get_data(event,&size);
                while(written<size){int count=send(connection->socket_fd,data+written,size-written,MSG_NOSIGNAL);if(count<=0){status=-1;break;}written+=count;}
                http_response_destroy(event);
                if(status<0)httpd_remove_connection(httpd,connection);
            }
        }
		/* Get the correct nfds value and set rfds */
		FD_ZERO(&rfds);
		if (httpd->open_connections < httpd->max_connections) {
			if (httpd->server_fd4 != -1) {
				FD_SET(httpd->server_fd4, &rfds);
				if (nfds <= httpd->server_fd4) {
					nfds = httpd->server_fd4+1;
				}
			}
			if (httpd->server_fd6 != -1) {
				FD_SET(httpd->server_fd6, &rfds);
				if (nfds <= httpd->server_fd6) {
					nfds = httpd->server_fd6+1;
				}
			}
		}
		for (i=0; i<httpd->max_connections; i++) {
			int socket_fd;
			if (!httpd->connections[i].connected) {
				continue;
			}
			socket_fd = httpd->connections[i].socket_fd;
			FD_SET(socket_fd, &rfds);
			if (nfds <= socket_fd) {
				nfds = socket_fd+1;
			}
		}

		ret = select(nfds, &rfds, NULL, NULL, &tv);
		if (ret == 0) {
			/* Timeout happened */
			continue;
		} else if (ret == -1) {
			/* FIXME: Error happened */
			logger_log(httpd->logger, LOGGER_INFO, "Error in select");
			break;
		}

		if (httpd->open_connections < httpd->max_connections &&
		    httpd->server_fd4 != -1 && FD_ISSET(httpd->server_fd4, &rfds)) {
			ret = httpd_accept_connection(httpd, httpd->server_fd4, 0);
			if (ret == -1) {
				break;
			} else if (ret == 0) {
				continue;
			}
		}
		if (httpd->open_connections < httpd->max_connections &&
		    httpd->server_fd6 != -1 && FD_ISSET(httpd->server_fd6, &rfds)) {
			ret = httpd_accept_connection(httpd, httpd->server_fd6, 1);
			if (ret == -1) {
				break;
			} else if (ret == 0) {
				continue;
			}
		}
		for (i=0; i<httpd->max_connections; i++) {
			http_connection_t *connection = &httpd->connections[i];

			if (!connection->connected) {
				continue;
			}
			if (!FD_ISSET(connection->socket_fd, &rfds)) {
				continue;
			}

			/* If not in the middle of request, allocate one */
			if (!connection->request) {
				connection->request = http_request_init();
				assert(connection->request);
			}

			logger_log(httpd->logger, LOGGER_DEBUG, "Receiving on socket %d", connection->socket_fd);
			ret = recv(connection->socket_fd, buffer, sizeof(buffer), 0);
			if (ret <= 0) {
                if(ret<0 && (errno==EINTR || errno==EAGAIN || errno==EWOULDBLOCK))continue;
				logger_log(httpd->logger, LOGGER_INFO, "Socket %d closed: %s errno=%d received=%lu requests=%u",
                    connection->socket_fd, ret == 0 ? "peer EOF" : "receive error", ret == 0 ? 0 : errno,
                    connection->received_bytes, connection->request_count);
				httpd_remove_connection(httpd, connection);
				continue;
			}

            connection->received_bytes += (unsigned)ret;
            {
                int available = ret, offset = 0;
                while (offset < available && connection->connected) {
                    int consumed;
                    if (!connection->request) connection->request = http_request_init();
                    assert(connection->request);
                    consumed = http_request_add_data(connection->request, buffer + offset, available - offset);
                    if (http_request_has_error(connection->request)) {
                        logger_log(httpd->logger, LOGGER_INFO, "Error in parsing: %s", http_request_get_error_name(connection->request));
                        httpd_remove_connection(httpd, connection);
                        break;
                    }
                    if (consumed < 0 || (consumed == 0 && !http_request_is_complete(connection->request))) {
                        logger_log(httpd->logger, LOGGER_INFO, "Parser made no progress on socket %d", connection->socket_fd);
                        httpd_remove_connection(httpd, connection); break;
                    }
                    offset += consumed;

                    /* If request is finished, process and deallocate */
                    if (http_request_is_complete(connection->request)) {
                        http_response_t *response = NULL;
                        httpd->callbacks.conn_request(connection->user_data, connection->request, &response);
                        httpd_trace_request(httpd, connection, connection->request, response);
                        http_request_destroy(connection->request);
                        connection->request = NULL;

                        if (response) {
                            const char *data;
                            int datalen;
                            int written;
                            int ret;

                            /* Get response data and datalen */
                            data = http_response_get_data(response, &datalen);

                            written = 0;
                            while (written < datalen) {
                                ret = send(connection->socket_fd, data+written, datalen-written, MSG_NOSIGNAL);
                                if (ret < 0 && errno == EINTR) continue;
                                if (ret <= 0) {
                                    /* Closed or failed peer. */
                                    logger_log(httpd->logger, LOGGER_INFO, "Send failed on socket %d errno=%d", connection->socket_fd, ret == 0 ? 0 : errno);
                                    break;
                                }
                                written += ret;
                            }

                            if (written != datalen || http_response_get_disconnect(response)) {
                                logger_log(httpd->logger, LOGGER_INFO, "Disconnecting on software request");
                                httpd_remove_connection(httpd, connection);
                            }
                        } else {
                            logger_log(httpd->logger, LOGGER_INFO, "Didn't get response");
                        }
                        http_response_destroy(response);
                    } else {
                        logger_log(httpd->logger, LOGGER_DEBUG, "Request not complete, waiting for more data...");
                        break;
                    }
                }
            }
		}
	}

	/* Remove all connections that are still connected */
	for (i=0; i<httpd->max_connections; i++) {
		http_connection_t *connection = &httpd->connections[i];

		if (!connection->connected) {
			continue;
		}
		logger_log(httpd->logger, LOGGER_INFO, "Removing connection for socket %d", connection->socket_fd);
		httpd_remove_connection(httpd, connection);
	}

	/* Close server sockets since they are not used any more */
	if (httpd->server_fd4 != -1) {
		shutdown(httpd->server_fd4, SHUT_RDWR);
		closesocket(httpd->server_fd4);
		httpd->server_fd4 = -1;
	}
	if (httpd->server_fd6 != -1) {
		shutdown(httpd->server_fd6, SHUT_RDWR);
		closesocket(httpd->server_fd6);
		httpd->server_fd6 = -1;
	}

	logger_log(httpd->logger, LOGGER_INFO, "Exiting HTTP thread: %d", httpd->thread);

	return 0;
}

int
httpd_start(httpd_t *httpd, unsigned short *port)
{
	/* How many connection attempts are kept in queue */
	int backlog = 5;

	assert(httpd);
	assert(port);

	MUTEX_LOCK(httpd->run_mutex);
	if (httpd->running || !httpd->joined) {
		MUTEX_UNLOCK(httpd->run_mutex);
		return 0;
	}

	httpd->server_fd4 = netutils_init_socket(port, 0, 0);
	if (httpd->server_fd4 == -1) {
		logger_log(httpd->logger, LOGGER_ERR, "Error initialising socket %d", SOCKET_GET_ERROR());
		MUTEX_UNLOCK(httpd->run_mutex);
		return -1;
	}
	httpd->server_fd6 = -1;/*= netutils_init_socket(port, 1, 0);
	if (httpd->server_fd6 == -1) {
		logger_log(httpd->logger, LOGGER_WARNING, "Error initialising IPv6 socket %d", SOCKET_GET_ERROR());
		logger_log(httpd->logger, LOGGER_WARNING, "Continuing without IPv6 support");
	}*/

	if (httpd->server_fd4 != -1 && listen(httpd->server_fd4, backlog) == -1) {
		logger_log(httpd->logger, LOGGER_ERR, "Error listening to IPv4 socket");
		closesocket(httpd->server_fd4);
		closesocket(httpd->server_fd6);
		MUTEX_UNLOCK(httpd->run_mutex);
		return -2;
	}
	if (httpd->server_fd6 != -1 && listen(httpd->server_fd6, backlog) == -1) {
		logger_log(httpd->logger, LOGGER_ERR, "Error listening to IPv6 socket");
		closesocket(httpd->server_fd4);
		closesocket(httpd->server_fd6);
		MUTEX_UNLOCK(httpd->run_mutex);
		return -2;
	}
	logger_log(httpd->logger, LOGGER_INFO, "Initialized server socket(s)");

	/* Set values correctly and create new thread */
	httpd->running = 1;
	httpd->joined = 0;
	THREAD_CREATE(httpd->thread, httpd_thread, httpd);
	MUTEX_UNLOCK(httpd->run_mutex);

	return 1;
}

int
httpd_is_running(httpd_t *httpd)
{
	int running;

	assert(httpd);

	MUTEX_LOCK(httpd->run_mutex);
	running = httpd->running || !httpd->joined;
	MUTEX_UNLOCK(httpd->run_mutex);

	return running;
}

void
httpd_stop(httpd_t *httpd)
{
	assert(httpd);

	MUTEX_LOCK(httpd->run_mutex);
	if (!httpd->running || httpd->joined) {
		MUTEX_UNLOCK(httpd->run_mutex);
		return;
	}
	logger_log(httpd->logger, LOGGER_INFO, "Stopping server socket..., %d", httpd->thread);
	httpd->running = 0;

	if (httpd->server_fd4 != -1) {
		shutdown(httpd->server_fd4, SHUT_RDWR);
		closesocket(httpd->server_fd4);
		httpd->server_fd4 = -1;
	}
	if (httpd->server_fd6 != -1) {
		shutdown(httpd->server_fd6, SHUT_RDWR);
		closesocket(httpd->server_fd6);
		httpd->server_fd6 = -1;
	}
	MUTEX_UNLOCK(httpd->run_mutex);

	THREAD_JOIN(httpd->thread);

	logger_log(httpd->logger, LOGGER_INFO, "Stopping server socket[joined]...");
	MUTEX_LOCK(httpd->run_mutex);
	httpd->joined = 1;
	MUTEX_UNLOCK(httpd->run_mutex);

	logger_log(httpd->logger, LOGGER_INFO, "Server socket stopped");
}

