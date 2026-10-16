#ifndef AIRTIZEN_ALAC_H
#define AIRTIZEN_ALAC_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct airtizen_alac airtizen_alac;
airtizen_alac *airtizen_alac_create(unsigned rate, unsigned channels, unsigned frames);
void airtizen_alac_destroy(airtizen_alac *decoder);
void airtizen_alac_flush(airtizen_alac *decoder);
/* 16-bit interleaved PCM. Returns bytes written, zero for rejected/no-output input. */
size_t airtizen_alac_decode(airtizen_alac *decoder, const uint8_t *packet, size_t packet_size,
                           int16_t *pcm, size_t pcm_capacity);
#ifdef __cplusplus
}
#endif
#endif
