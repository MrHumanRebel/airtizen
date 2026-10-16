#ifndef AIRTIZEN_PLAYBACK_H
#define AIRTIZEN_PLAYBACK_H
#include <stddef.h>
typedef struct {
    unsigned generation,command;
    int active,ready,failed;
    double fraction,position,duration,rate,seek;
    char url[2049];
} airtizen_playback_state;
int airtizen_playback_play(const char *url,double start_fraction);
void airtizen_playback_stop(void);
int airtizen_playback_rate(double rate);
int airtizen_playback_seek(double position);
int airtizen_playback_report(unsigned generation,double position,double duration,int ready,int failed);
void airtizen_playback_snapshot(airtizen_playback_state *state);
#endif
