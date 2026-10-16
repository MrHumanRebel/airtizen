/* Exercise real AirTizen callbacks with deterministic donor/audio boundaries. */
#include "airtizen_core.h"
#include "airtizen_json.h"
#include "raop.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static raop_callbacks_t callbacks;
static int16_t received[16];
static size_t received_bytes;
static int open_result;
raop_t *raop_init(int clients, raop_callbacks_t *cb) { (void)clients; callbacks=*cb; return (raop_t *)1; }
void raop_set_log_level(raop_t *r,int l) { (void)r;(void)l; }
void raop_set_log_callback(raop_t *r,raop_log_callback_t cb,void *cls) { (void)r;(void)cb;(void)cls; }
int raop_start(raop_t *r,unsigned short *p) { (void)r;(void)p;return 1; }
void raop_stop(raop_t *r) { (void)r; }
void raop_destroy(raop_t *r) { (void)r; }
int airtizen_audio_open(int rate,int channels,int bits) { (void)rate;(void)channels;(void)bits;return open_result; }
int airtizen_audio_write(const void *data,size_t bytes) {
    size_t n=bytes>4?4:bytes; /* Real callback must handle partial device writes. */
    assert(received_bytes+n<=sizeof(received));
    memcpy((unsigned char *)received+received_bytes,data,n); received_bytes+=n; return (int)n;
}
void airtizen_audio_close(void) {}
int main(void) {
    char quoted[128], small[3], logs[10000];
    airtizen_core_state state;
    int16_t pcm[]={12000,-12000,32767,-32768};
    pcm_data_struct audio={0};
    h264_decode_struct video={0};
    assert(airtizen_json_quote(quoted,sizeof(quoted),"\"\\\n\001")>0);
    assert(strcmp(quoted,"\"\\\"\\\\\\u000a\\u0001\"")==0);
    assert(airtizen_json_quote(small,sizeof(small),"x")==-1);
    assert(airtizen_json_quote(quoted,sizeof(quoted),"\xff")==8);
    assert(strcmp(quoted,"\"\\ufffd\"")==0);
    assert(airtizen_json_quote(quoted,sizeof(quoted),"Árvíz")>0);
    assert(strcmp(quoted,"\"Árvíz\"")==0);
    assert(airtizen_core_start("Test")==0);
    callbacks.audio_format(NULL,2);
    airtizen_core_snapshot(&state);assert(strcmp(state.codec,"ALAC")==0);
    callbacks.connected(NULL,"Phone \"test\"",NULL);
    airtizen_core_snapshot(&state); assert(state.connected);
    callbacks.audio_set_volume(NULL,NULL,-6.0206f,NULL,NULL);
    audio.data=(unsigned short *)pcm; audio.data_len=sizeof(pcm);
    audio.sample_rate=44100; audio.channels=2; audio.bits_per_sample=16;
    callbacks.audio_process(NULL,&audio,"Phone",NULL);
    assert(received_bytes==sizeof(pcm));
    assert(abs(received[0]-6000)<=1 && abs(received[1]+6000)<=1);
    airtizen_core_snapshot(&state);
    assert(state.audio_bytes==sizeof(pcm) && state.sample_rate==44100);
    assert(strcmp(state.audio,"host_stub")==0);
    received_bytes=0;
    callbacks.audio_set_volume(NULL,NULL,-144,NULL,NULL);
    callbacks.audio_process(NULL,&audio,"Phone",NULL);
    assert(received[0]==0 && received[1]==0);
    callbacks.audio_set_volume(NULL,NULL,NAN,NULL,NULL);
    received_bytes=0; callbacks.audio_process(NULL,&audio,"Phone",NULL);
    assert(received[0]==0);
    video.data=(unsigned char *)pcm;video.data_len=sizeof(pcm);video.frame_type=1;
    callbacks.video_process(NULL,&video,"Phone",NULL);
    airtizen_core_snapshot(&state);assert(state.video_frames==1);
    assert(strcmp(state.last_error,"invalid_video_stream")==0);
    open_result=-1;callbacks.audio_process(NULL,&audio,"Phone",NULL);
    airtizen_core_snapshot(&state);assert(strcmp(state.last_error,"audio_open_failed")==0);
    open_result=0; audio.data_len=3;callbacks.audio_process(NULL,&audio,"Phone",NULL);
    airtizen_core_snapshot(&state);assert(strcmp(state.last_error,"invalid_pcm_format")==0);
    airtizen_core_logs_json(logs,sizeof(logs));assert(strstr(logs,"audio_open_failed"));
    airtizen_core_stop();airtizen_core_snapshot(&state);assert(!state.running && !state.connected);
    puts("Core callbacks, partial writes, gain, mute, errors, JSON and logs passed");
    return 0;
}
