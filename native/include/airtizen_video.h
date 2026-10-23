#ifndef AIRTIZEN_VIDEO_H
#define AIRTIZEN_VIDEO_H
#include <stddef.h>
#include <stdint.h>
typedef struct {
    unsigned generation, width, height;
    uint64_t latest, start;
    int available;
    char mime[80];
} airtizen_video_state;
void airtizen_video_reset(void);
/* Annex-B parameter sets and access units. Timestamps are microseconds. */
int airtizen_video_configure(const uint8_t *data, size_t size);
int airtizen_video_frame(const uint8_t *data, size_t size, uint64_t pts);
size_t airtizen_video_hls_manifest(const char *token,char **out);
size_t airtizen_video_hls_segment(unsigned generation,uint64_t first,uint64_t last,uint8_t **out);
void airtizen_video_snapshot(airtizen_video_state *state);
/* Copies owned by caller. init=1 selects initialization data, otherwise sequence. */
size_t airtizen_video_copy(int init, uint64_t sequence, uint8_t **data, unsigned *generation);
#endif
