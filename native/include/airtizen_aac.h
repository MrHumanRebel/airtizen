#ifndef AIRTIZEN_AAC_H
#define AIRTIZEN_AAC_H
#include <stddef.h>
#include <stdint.h>
typedef struct airtizen_aac airtizen_aac;
airtizen_aac *airtizen_aac_create(unsigned codec_type,unsigned rate,unsigned channels,unsigned frames);
void airtizen_aac_destroy(airtizen_aac *decoder);
void airtizen_aac_flush(airtizen_aac *decoder);
size_t airtizen_aac_decode(airtizen_aac *decoder,const uint8_t *data,size_t size,int16_t *pcm,size_t capacity);
#endif
