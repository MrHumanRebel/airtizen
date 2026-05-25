#ifndef AIRTIZEN_RUNTIME_H
#define AIRTIZEN_RUNTIME_H

#ifdef __cplusplus
extern "C" {
#endif

int airtizen_runtime_start(const char *device_name);
void airtizen_runtime_stop(void);
const char *airtizen_runtime_status_json(void);

#ifdef __cplusplus
}
#endif

#endif
