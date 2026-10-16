#include "airtizen_aac.h"
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/mem.h>
#include <libavutil/samplefmt.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
struct airtizen_aac {AVCodecContext *context;AVFrame *frame;unsigned rate,channels,frames;};
airtizen_aac *airtizen_aac_create(unsigned type,unsigned rate,unsigned channels,unsigned frames) {
    airtizen_aac *d;const AVCodec *codec;uint8_t config[4]={0xf8,0xe8,0x50,0};unsigned length=4;
    if(channels!=2 || (rate!=44100 && rate!=48000))return NULL;
    if(type==4){if(frames!=1024)return NULL;config[0]=rate==48000?0x11:0x12;config[1]=rate==48000?0x90:0x10;length=2;}
    else if(type!=8 || rate!=44100 || frames!=480)return NULL;
    codec=avcodec_find_decoder(AV_CODEC_ID_AAC);if(!codec)return NULL;
    d=calloc(1,sizeof(*d));if(!d)return NULL;
    d->context=avcodec_alloc_context3(codec);d->frame=av_frame_alloc();
    if(!d->context || !d->frame)goto failed;
    d->context->extradata=av_mallocz(length+AV_INPUT_BUFFER_PADDING_SIZE);if(!d->context->extradata)goto failed;
    memcpy(d->context->extradata,config,length);d->context->extradata_size=(int)length;
    d->context->sample_rate=(int)rate;d->context->thread_count=1;
    av_channel_layout_default(&d->context->ch_layout,(int)channels);
    d->rate=rate;d->channels=channels;d->frames=frames;
    if(avcodec_open2(d->context,codec,NULL)<0)goto failed;
    return d;
failed:airtizen_aac_destroy(d);return NULL;
}
void airtizen_aac_destroy(airtizen_aac *d){if(!d)return;av_frame_free(&d->frame);avcodec_free_context(&d->context);free(d);}
void airtizen_aac_flush(airtizen_aac *d){if(d){avcodec_flush_buffers(d->context);av_frame_unref(d->frame);}}
size_t airtizen_aac_decode(airtizen_aac *d,const uint8_t *data,size_t size,int16_t *pcm,size_t capacity) {
    AVPacket *packet;AVFrame *frame;int result;size_t bytes,i;unsigned channel;
    if(!d || !data || !pcm || !size || size>32768)return 0;
    packet=av_packet_alloc();if(!packet)return 0;
    if(av_new_packet(packet,(int)size)<0){av_packet_free(&packet);return 0;}
    memcpy(packet->data,data,size);result=avcodec_send_packet(d->context,packet);av_packet_free(&packet);if(result<0)return 0;
    frame=d->frame;if(avcodec_receive_frame(d->context,frame)<0)return 0;
    bytes=(size_t)frame->nb_samples*d->channels*2;
    if(frame->nb_samples!=(int)d->frames || frame->ch_layout.nb_channels!=(int)d->channels || frame->sample_rate!=(int)d->rate || bytes>capacity)goto rejected;
    if(frame->format==AV_SAMPLE_FMT_S16)memcpy(pcm,frame->data[0],bytes);
    else if(frame->format==AV_SAMPLE_FMT_S16P){
        for(i=0;i<d->frames;++i)for(channel=0;channel<d->channels;++channel)
            memcpy(pcm+i*d->channels+channel,frame->extended_data[channel]+i*2,2);
    } else if(frame->format==AV_SAMPLE_FMT_FLTP || frame->format==AV_SAMPLE_FMT_FLT){
        int planar=frame->format==AV_SAMPLE_FMT_FLTP;
        for(i=0;i<d->frames;++i)for(channel=0;channel<d->channels;++channel){
            float sample;size_t index=planar?i:i*d->channels+channel;
            memcpy(&sample,frame->extended_data[planar?channel:0]+index*sizeof(float),sizeof(sample));
            if(!isfinite(sample))goto rejected;
            sample*=32768.0f;if(sample>32767.0f)sample=32767.0f;if(sample< -32768.0f)sample= -32768.0f;
            pcm[i*d->channels+channel]=(int16_t)sample;
        }
    } else goto rejected;
    av_frame_unref(frame);return bytes;
rejected:av_frame_unref(frame);return 0;
}
