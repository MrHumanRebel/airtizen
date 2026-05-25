#ifndef AIRTIZEN_MDNS_H
#define AIRTIZEN_MDNS_H

#ifdef __cplusplus
extern "C" {
#endif

int airtizen_mdns_start(const char *device_name, const char *device_id, unsigned short raop_port);
void airtizen_mdns_stop(void);
int airtizen_mdns_is_running(void);

#ifdef __cplusplus
}
#endif

#endif
