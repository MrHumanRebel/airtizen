#include "airtizen_alac.h"
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/mem.h>
#include <libavutil/samplefmt.h>
#include <stdlib.h>
#include <string.h>
struct airtizen_alac { AVCodecContext *context; AVFrame *frame; unsigned frames, channels; };
static void put32(uint8_t *p, uint32_t value) {
    p[0]=(uint8_t)(value>>24);p[1]=(uint8_t)(value>>16);p[2]=(uint8_t)(value>>8);p[3]=(uint8_t)value;
}
airtizen_alac *airtizen_alac_create(unsigned rate, unsigned channels, unsigned frames) {
    airtizen_alac *decoder;
    const AVCodec *codec;
    uint8_t *config;
    if ((rate!=44100 && rate!=48000) || channels<1 || channels>2 || frames<1 || frames>4096) return NULL;
    codec=avcodec_find_decoder(AV_CODEC_ID_ALAC);
    if (!codec) return NULL;
    decoder=calloc(1,sizeof(*decoder));
    if (!decoder) return NULL;
    decoder->context=avcodec_alloc_context3(codec);decoder->frame=av_frame_alloc();
    if (!decoder->context || !decoder->frame) goto failed;
    decoder->context->extradata=av_mallocz(36+AV_INPUT_BUFFER_PADDING_SIZE);
    if (!decoder->context->extradata) goto failed;
    decoder->context->extradata_size=36;
    config=decoder->context->extradata;
    put32(config,36);memcpy(config+4,"alac",4);
    config+=12;put32(config,frames);
    config[5]=16;config[6]=40;config[7]=10;config[8]=14;config[9]=(uint8_t)channels;
    config[10]=0;config[11]=255;put32(config+20,rate);
    decoder->context->sample_rate=(int)rate;
    decoder->context->thread_count=1;
    av_channel_layout_default(&decoder->context->ch_layout,(int)channels);
    decoder->frames=frames;decoder->channels=channels;
    if (avcodec_open2(decoder->context,codec,NULL)<0) goto failed;
    return decoder;
failed:
    airtizen_alac_destroy(decoder);return NULL;
}
void airtizen_alac_destroy(airtizen_alac *decoder) {
    if (!decoder) return;
    av_frame_free(&decoder->frame);avcodec_free_context(&decoder->context);free(decoder);
}
void airtizen_alac_flush(airtizen_alac *decoder) {
    if (decoder) { avcodec_flush_buffers(decoder->context);av_frame_unref(decoder->frame); }
}
size_t airtizen_alac_decode(airtizen_alac *decoder,const uint8_t *data,size_t size,int16_t *pcm,size_t capacity) {
    AVPacket *packet;
    AVFrame *frame;
    size_t bytes,i;
    unsigned channel;
    int rc;
    if (!decoder || !data || !pcm || !size || size>32768) return 0;
    packet=av_packet_alloc();if (!packet) return 0;
    if (av_new_packet(packet,(int)size)<0) { av_packet_free(&packet);return 0; }
    memcpy(packet->data,data,size);
    rc=avcodec_send_packet(decoder->context,packet);av_packet_free(&packet);
    if (rc<0) return 0;
    frame=decoder->frame;
    if (avcodec_receive_frame(decoder->context,frame)<0) return 0;
    if (frame->nb_samples<1 || (unsigned)frame->nb_samples>decoder->frames ||
        frame->ch_layout.nb_channels!=(int)decoder->channels || frame->sample_rate!=decoder->context->sample_rate ||
        (frame->format!=AV_SAMPLE_FMT_S16 && frame->format!=AV_SAMPLE_FMT_S16P)) {
        av_frame_unref(frame);return 0;
    }
    bytes=(size_t)frame->nb_samples*decoder->channels*sizeof(int16_t);
    if (bytes>capacity) { av_frame_unref(frame);return 0; }
    if (frame->format==AV_SAMPLE_FMT_S16) memcpy(pcm,frame->data[0],bytes);
    else for (i=0;i<(size_t)frame->nb_samples;++i)
        for (channel=0;channel<decoder->channels;++channel)
            memcpy(pcm+i*decoder->channels+channel,frame->extended_data[channel]+i*sizeof(int16_t),sizeof(int16_t));
    av_frame_unref(frame);return bytes;
}
