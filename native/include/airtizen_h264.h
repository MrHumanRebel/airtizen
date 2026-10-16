#ifndef AIRTIZEN_H264_H
#define AIRTIZEN_H264_H
#include <stddef.h>
#include <stdint.h>
/* Converts bounded AVC configuration to Annex-B, or validates/converts AVCC access units. */
size_t airtizen_h264_config(const uint8_t *data,size_t size,uint8_t *out,size_t capacity);
int airtizen_h264_frame(uint8_t *data,size_t size);
#endif
