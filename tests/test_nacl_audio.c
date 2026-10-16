#include "audio_queue.h"
#include <assert.h>
#include <string.h>
int main(void){
    airtizen_audio_queue q;int16_t mono[]={10,-20,30},stereo[8],input[AIRTIZEN_AUDIO_FRAMES*2];unsigned i;
    airtizen_audio_queue_init(&q);
    assert(airtizen_audio_queue_write(&q,mono,sizeof(mono))<0);
    airtizen_audio_queue_open(&q,1);
    assert(airtizen_audio_queue_write(&q,mono,1)<0);
    assert(airtizen_audio_queue_write(&q,mono,sizeof(mono))==sizeof(mono));
    memset(stereo,127,sizeof(stereo));airtizen_audio_queue_render(&q,stereo,sizeof(stereo));
    for(i=0;i<3;++i)assert(stereo[i*2]==mono[i] && stereo[i*2+1]==mono[i]);
    assert(stereo[6]==0 && stereo[7]==0 && q.underruns==1);
    airtizen_audio_queue_open(&q,2);
    for(i=0;i<AIRTIZEN_AUDIO_FRAMES*2;++i)input[i]=(int16_t)i;
    assert(airtizen_audio_queue_write(&q,input,sizeof(input))==sizeof(input));
    assert(airtizen_audio_queue_write(&q,input,4)==0); /* Never overwrite queued audio. */
    airtizen_audio_queue_render(&q,stereo,sizeof(stereo));assert(!memcmp(stereo,input,sizeof(stereo)));
    assert(airtizen_audio_queue_write(&q,input,16)==16); /* Wrap-around. */
    airtizen_audio_queue_close(&q);memset(stereo,127,sizeof(stereo));airtizen_audio_queue_render(&q,stereo,sizeof(stereo));
    for(i=0;i<8;++i)assert(stereo[i]==0);
    airtizen_audio_queue_destroy(&q);return 0;
}
