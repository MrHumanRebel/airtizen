#ifndef AIRTIZEN_METADATA_H
#define AIRTIZEN_METADATA_H
#include <stddef.h>
#include <stdint.h>
typedef struct {char title[256],artist[256],album[256];unsigned artwork;uint32_t progress,total;} airtizen_metadata_state;
int airtizen_metadata_parse(const void *data,size_t size);
int airtizen_metadata_artwork(const void *data,size_t size);
void airtizen_metadata_progress(uint32_t start,uint32_t current,uint32_t end);
void airtizen_metadata_reset(void);
void airtizen_metadata_snapshot(airtizen_metadata_state *out);
size_t airtizen_metadata_copy_artwork(unsigned version,uint8_t **data,char mime[16]);
#endif
