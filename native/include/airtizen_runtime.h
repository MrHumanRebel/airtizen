#ifndef AIRTIZEN_RUNTIME_H
#define AIRTIZEN_RUNTIME_H

#ifdef __cplusplus
extern "C" {
#endif

int airtizen_runtime_start(const char *device_name);
void airtizen_runtime_stop(void);
/* Changes only discovery/negotiation metadata; restarts an active receiver. */
int airtizen_runtime_set_discovery_profile(const char *profile);
const char *airtizen_runtime_status_json(void);

#ifdef __cplusplus
}
#endif

#endif
