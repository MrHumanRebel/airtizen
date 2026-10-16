#ifndef AIRTIZEN_PHOTO_H
#define AIRTIZEN_PHOTO_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define AIRTIZEN_PHOTO_MAX_BYTES (8U*1024U*1024U)
typedef struct {
    unsigned generation,width,height,session_id;
    int active,dissolve,ready,failed;
    char session[65];
} airtizen_photo_state;
/* Returns an HTTP status; cacheOnly never replaces the displayed photo. */
int airtizen_photo_put(const char *action,const char *key,const char *session,const char *transition,const void *data,size_t size);
void airtizen_photo_reset(void);
void airtizen_photo_snapshot(airtizen_photo_state *out);
size_t airtizen_photo_copy(unsigned generation,uint8_t **out,char mime[16]);
int airtizen_photo_report(unsigned generation,int failed);
#ifdef __cplusplus
}
#endif
#endif
