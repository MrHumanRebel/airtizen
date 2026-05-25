#ifndef AIRTIZEN_CORE_H
#define AIRTIZEN_CORE_H
#ifdef __cplusplus
extern "C" {
#endif
int airtizen_core_start(const char *device_name);
void airtizen_core_stop(void);
const char *airtizen_core_status_json(void);
#ifdef __cplusplus
}
#endif
#endif
