#ifndef AIRTIZEN_CONTROL_HTTP_H
#define AIRTIZEN_CONTROL_HTTP_H

#ifdef __cplusplus
extern "C" {
#endif

/* Configure before starting. Exactly 32 random bytes encoded as 64 hex digits. */
int airtizen_control_http_set_token(const char *token);
int airtizen_control_http_start(unsigned short port);
void airtizen_control_http_stop(void);

#ifdef __cplusplus
}
#endif

#endif
