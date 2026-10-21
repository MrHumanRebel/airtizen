#ifndef AIRTIZEN_MDNS_H
#define AIRTIZEN_MDNS_H
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Set a sandbox-provided LAN address and stable local receiver identity before start. */
int airtizen_mdns_set_network(const char *ipv4, const char *device_id);
int airtizen_mdns_set_public_key(const unsigned char key[32]);
void airtizen_mdns_pairing_id(char uuid[37]);
/* DNS-SD TXT wire bytes, identical to the corresponding multicast record. */
size_t airtizen_mdns_txt(int airplay, unsigned char *out, size_t capacity);
int airtizen_mdns_start(const char *device_name, const char *device_id, unsigned short raop_port);
const char *airtizen_mdns_last_error(void);
void airtizen_mdns_stop(void);
int airtizen_mdns_is_running(void);
void airtizen_mdns_identity(char *name, size_t name_size, char *id, size_t id_size);

#ifdef __cplusplus
}
#endif

#endif
