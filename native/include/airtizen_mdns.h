#ifndef AIRTIZEN_MDNS_H
#define AIRTIZEN_MDNS_H
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int airtizen_mdns_start(const char *device_name, const char *device_id, unsigned short raop_port);
void airtizen_mdns_stop(void);
int airtizen_mdns_is_running(void);
void airtizen_mdns_identity(char *name, size_t name_size, char *id, size_t id_size);

#ifdef __cplusplus
}
#endif

#endif
