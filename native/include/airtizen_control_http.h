#ifndef AIRTIZEN_CONTROL_HTTP_H
#define AIRTIZEN_CONTROL_HTTP_H

#ifdef __cplusplus
extern "C" {
#endif

int airtizen_control_http_start(unsigned short port);
void airtizen_control_http_stop(void);

#ifdef __cplusplus
}
#endif

#endif
