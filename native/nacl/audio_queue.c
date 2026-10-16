#include "audio_queue.h"
#include <string.h>
void airtizen_audio_queue_init(airtizen_audio_queue *q) {
    memset(q,0,sizeof(*q));pthread_mutex_init(&q->mutex,NULL);
}
void airtizen_audio_queue_destroy(airtizen_audio_queue *q) {pthread_mutex_destroy(&q->mutex);}
void airtizen_audio_queue_open(airtizen_audio_queue *q,unsigned channels) {
    pthread_mutex_lock(&q->mutex);q->read_frame=q->count=0;q->rendered_frames=q->underruns=0;
    q->channels=channels;q->open=channels==1 || channels==2;pthread_mutex_unlock(&q->mutex);
}
void airtizen_audio_queue_close(airtizen_audio_queue *q) {
    pthread_mutex_lock(&q->mutex);q->open=0;q->count=q->read_frame=0;pthread_mutex_unlock(&q->mutex);
}
int airtizen_audio_queue_write(airtizen_audio_queue *q,const void *pcm,size_t bytes) {
    const char *in=pcm;size_t frames,i;int result;
    if(!pcm)return -1;
    pthread_mutex_lock(&q->mutex);
    if(!q->open || bytes%(q->channels*2)){pthread_mutex_unlock(&q->mutex);return -1;}
    frames=bytes/(q->channels*2);if(frames>AIRTIZEN_AUDIO_FRAMES-q->count)frames=AIRTIZEN_AUDIO_FRAMES-q->count;
    for(i=0;i<frames;++i) {
        size_t index=(q->read_frame+q->count+i)%AIRTIZEN_AUDIO_FRAMES;
        /* memcpy permits an unaligned input RTP decoder buffer. */
        memcpy(&q->samples[index*2],(const char*)in+i*q->channels*2,2);
        memcpy(&q->samples[index*2+1],(const char*)in+(i*q->channels+(q->channels==2))*2,2);
    }
    q->count+=frames;result=(int)(frames*q->channels*2);pthread_mutex_unlock(&q->mutex);return result;
}
void airtizen_audio_queue_render(airtizen_audio_queue *q,void *output,size_t bytes) {
    size_t frames=bytes/4,i;int16_t *out=output;
    memset(output,0,bytes);pthread_mutex_lock(&q->mutex);
    if(!q->open){pthread_mutex_unlock(&q->mutex);return;}
    if(frames>q->count){++q->underruns;frames=q->count;}
    for(i=0;i<frames;++i){size_t index=(q->read_frame+i)%AIRTIZEN_AUDIO_FRAMES;memcpy(out+i*2,q->samples+index*2,4);}
    q->read_frame=(q->read_frame+frames)%AIRTIZEN_AUDIO_FRAMES;q->count-=frames;q->rendered_frames+=frames;
    pthread_mutex_unlock(&q->mutex);
}
