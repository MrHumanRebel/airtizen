#include "airtizen_video.h"
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#include <libavutil/mem.h>
#include <pthread.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __native_client__
extern void airtizen_nacl_video_configure(const uint8_t *,size_t);
extern void airtizen_nacl_video_frame(const uint8_t *,size_t);
extern void airtizen_nacl_video_reset(void);
#endif
#define PART_LIMIT (2U*1024U*1024U)
#define TOTAL_LIMIT (24U*1024U*1024U)
#define PART_COUNT 256
#define HLS_GROUP 15
struct part { uint8_t *data;size_t size;uint64_t seq,pts;int key; };
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static AVFormatContext *mux;
static AVIOContext *io;
static uint8_t *config,*init_data,*pending;
static size_t config_size,init_size,pending_size,total_size;
static struct part parts[PART_COUNT];
static airtizen_video_state state;
static uint64_t sequence,first_pts,last_pts;
static int have_pts,write_failed;

static int output(void *opaque,const uint8_t *data,int size) {
    uint8_t *next;
    (void)opaque;
    if (size<0 || (size_t)size>PART_LIMIT-pending_size) { write_failed=1;return AVERROR(ENOSPC); }
    next=av_realloc(pending,pending_size+(size_t)size);
    if (!next) { write_failed=1;return AVERROR(ENOMEM); }
    pending=next;memcpy(pending+pending_size,data,(size_t)size);pending_size+=(size_t)size;
    return size;
}
static int nalu_type(const uint8_t *data,size_t size,unsigned wanted) {
    size_t i;
    for (i=0;i+4<size;++i) {
        size_t prefix=0;
        if (!data[i] && !data[i+1] && data[i+2]==1) prefix=3;
        else if (i+5<=size && !data[i] && !data[i+1] && !data[i+2] && data[i+3]==1) prefix=4;
        if (prefix && (data[i+prefix]&31)==wanted) return 1;
    }
    return 0;
}
static void clear_locked(void) {
    unsigned i;
    if (mux) { mux->pb=NULL;avformat_free_context(mux);mux=NULL; }
    if (io) { av_freep(&io->buffer);avio_context_free(&io); }
    av_freep(&config);config_size=0;av_freep(&init_data);init_size=0;
    av_freep(&pending);pending_size=0;total_size=0;sequence=0;have_pts=0;write_failed=0;
    for (i=0;i<PART_COUNT;++i) { av_freep(&parts[i].data);memset(parts+i,0,sizeof(parts[i])); }
    memset(&state,0,sizeof(state));
}
void airtizen_video_reset(void) {
    unsigned generation;
    pthread_mutex_lock(&lock);generation=state.generation+1;clear_locked();state.generation=generation;pthread_mutex_unlock(&lock);
#ifdef __native_client__
    airtizen_nacl_video_reset();
#endif
}
int airtizen_video_configure(const uint8_t *data,size_t size) {
    unsigned generation;size_t i;
    if (!data || size<8 || size>65536 || !nalu_type(data,size,7) || !nalu_type(data,size,8)) return -1;
    pthread_mutex_lock(&lock);
    generation=state.generation+1;clear_locked();state.generation=generation;
    config=av_mallocz(size+AV_INPUT_BUFFER_PADDING_SIZE);
    if (!config) { pthread_mutex_unlock(&lock);return -1; }
    memcpy(config,data,size);config_size=size;
    for (i=0;i+8<size;++i) if (!data[i] && !data[i+1] && !data[i+2] && data[i+3]==1 && (data[i+4]&31)==7) {
        snprintf(state.mime,sizeof(state.mime),"video/mp4; codecs=\"avc1.%02x%02x%02x\"",data[i+5],data[i+6],data[i+7]);break;
    }
    pthread_mutex_unlock(&lock);
#ifdef __native_client__
    airtizen_nacl_video_configure(data,size);
#endif
    return 0;
}
static int initialize(const uint8_t *data,size_t size) {
    AVCodecContext *decoder=NULL;
    AVFrame *frame=NULL;
    AVPacket *packet=NULL;
    AVStream *stream;
    AVDictionary *options=NULL;
    uint8_t *buffer=NULL;
    int result=-1;
    decoder=avcodec_alloc_context3(avcodec_find_decoder(AV_CODEC_ID_H264));
    packet=av_packet_alloc();frame=av_frame_alloc();
    if (!decoder || !packet || !frame) goto end;
    decoder->thread_count=1;decoder->max_pixels=4096*2160;
    if (avcodec_open2(decoder,decoder->codec,NULL)<0 || av_new_packet(packet,(int)(config_size+size))<0) goto end;
    memcpy(packet->data,config,config_size);memcpy(packet->data+config_size,data,size);
    if (avcodec_send_packet(decoder,packet)<0) goto end;
    (void)avcodec_receive_frame(decoder,frame);
    if (decoder->width<1 || decoder->height<1 || decoder->width>4096 || decoder->height>2160) goto end;
    state.width=(unsigned)decoder->width;state.height=(unsigned)decoder->height;
    if (avformat_alloc_output_context2(&mux,NULL,"mp4",NULL)<0 || !mux) goto end;
    stream=avformat_new_stream(mux,NULL);if (!stream) goto end;
    stream->time_base=(AVRational){1,1000000};
    stream->codecpar->codec_type=AVMEDIA_TYPE_VIDEO;stream->codecpar->codec_id=AV_CODEC_ID_H264;
    stream->codecpar->width=decoder->width;stream->codecpar->height=decoder->height;
    stream->codecpar->extradata=av_mallocz(config_size+AV_INPUT_BUFFER_PADDING_SIZE);
    if (!stream->codecpar->extradata) goto end;
    memcpy(stream->codecpar->extradata,config,config_size);stream->codecpar->extradata_size=(int)config_size;
    buffer=av_malloc(32768);if (!buffer) goto end;
    io=avio_alloc_context(buffer,32768,1,NULL,NULL,output,NULL);if (!io) { av_free(buffer);goto end; }
    mux->pb=io;mux->flags|=AVFMT_FLAG_CUSTOM_IO;
    av_dict_set(&options,"movflags","empty_moov+default_base_moof+frag_every_frame",0);
    if (avformat_write_header(mux,&options)<0) goto end;
    avio_flush(io);
    if (write_failed || !pending_size) goto end;
    init_data=pending;init_size=pending_size;pending=NULL;pending_size=0;
    result=0;
end:
    av_dict_free(&options);av_frame_free(&frame);av_packet_free(&packet);avcodec_free_context(&decoder);
    return result;
}
int airtizen_video_frame(const uint8_t *data,size_t size,uint64_t pts) {
    AVPacket *packet=NULL;
    int key,result=-1;
    unsigned i;
    struct part *part;
    if (!data || !size || size>PART_LIMIT || pts>INT64_MAX) return -1;
    key=nalu_type(data,size,5);
    pthread_mutex_lock(&lock);
    if (!config) goto end;
    if (!mux) {
        if (!key) { result=0;goto end; }
        if (initialize(data,size)<0) goto failed;
    }
    if (have_pts && pts<=last_pts) goto end;
    if (!have_pts) { first_pts=pts;have_pts=1; }
    last_pts=pts;
    packet=av_packet_alloc();if (!packet || av_new_packet(packet,(int)size)<0) goto failed;
    memcpy(packet->data,data,size);packet->stream_index=0;
    packet->pts=packet->dts=av_rescale_q((int64_t)(pts-first_pts),(AVRational){1,1000000},mux->streams[0]->time_base);
    packet->duration=av_rescale_q(33333,(AVRational){1,1000000},mux->streams[0]->time_base);
    if (key) packet->flags|=AV_PKT_FLAG_KEY;
    if (av_write_frame(mux,packet)<0 || av_write_frame(mux,NULL)<0) goto failed;
    avio_flush(io);if (write_failed || !pending_size) goto failed;
    part=parts+(++sequence%PART_COUNT);
    total_size-=part->size;av_freep(&part->data);
    part->seq=sequence;part->pts=pts-first_pts;part->size=pending_size;part->data=pending;part->key=key;
    total_size+=pending_size;pending=NULL;pending_size=0;state.latest=sequence;
    while (total_size>TOTAL_LIMIT) {
        struct part *oldest=NULL;
        for(i=0;i<PART_COUNT;++i)if(parts[i].data && (!oldest || parts[i].seq<oldest->seq))oldest=parts+i;
        if (!oldest)break;
        total_size-=oldest->size;av_freep(&oldest->data);oldest->size=0;
    }
    state.start=0;
    for(i=0;i<PART_COUNT;++i)if(parts[i].data && parts[i].key && parts[i].seq>state.start)state.start=parts[i].seq;
    state.available=state.start!=0;
#ifdef __native_client__
    airtizen_nacl_video_frame(data,size);
#endif
    result=1;goto end;
failed:
    { unsigned generation=state.generation+1;clear_locked();state.generation=generation; }
end:
    av_packet_free(&packet);pthread_mutex_unlock(&lock);return result;
}
void airtizen_video_snapshot(airtizen_video_state *out) { pthread_mutex_lock(&lock);*out=state;pthread_mutex_unlock(&lock); }
size_t airtizen_video_copy(int init,uint64_t seq,uint8_t **data,unsigned *generation) {
    const uint8_t *source=NULL;size_t size=0;
    *data=NULL;
    pthread_mutex_lock(&lock);*generation=state.generation;
    if (init) { source=init_data;size=init_size; }
    else { struct part *part=parts+(seq%PART_COUNT);if(part->seq==seq && part->data){source=part->data;size=part->size;} }
    if (size) { *data=malloc(size);if(*data)memcpy(*data,source,size);else size=0; }
    pthread_mutex_unlock(&lock);return size;
}

/* HLS batches existing fMP4 fragments; no decoding or re-encoding is added.
 * The manifest and each read are copied under the same video mutex. */
size_t airtizen_video_hls_manifest(const char *token,char **out) {
    char text[8192];size_t used=0;uint64_t group,first_group,last_group,first,last,i;
    double duration,max_duration=1;int valid;unsigned target;
    *out=NULL;pthread_mutex_lock(&lock);
    if(!init_size || !sequence)goto done;
    last_group=state.latest/HLS_GROUP;if(!last_group)goto done;
    --last_group;first_group=last_group>5?last_group-5:0;
    /* Advance past evicted or partial groups; all advertised bytes must exist. */
    for(group=first_group;group<=last_group;++group) {
        valid=1;first=group*HLS_GROUP+1;last=first+HLS_GROUP-1;
        for(i=first;i<=last;++i)if(parts[i%PART_COUNT].seq!=i || !parts[i%PART_COUNT].data){valid=0;break;}
        if(valid)break;
    }
    first_group=group;if(first_group>last_group)goto done;
    for(group=first_group;group<=last_group;++group) {
        first=group*HLS_GROUP+1;last=first+HLS_GROUP-1;
        duration=(parts[last%PART_COUNT].pts+33333-parts[first%PART_COUNT].pts)/1000000.0;
        if(duration>max_duration)max_duration=duration;
    }
    target=(unsigned)max_duration;if((double)target<max_duration)++target;
    used=(size_t)snprintf(text,sizeof(text),"#EXTM3U\n#EXT-X-VERSION:7\n#EXT-X-TARGETDURATION:%u\n#EXT-X-MEDIA-SEQUENCE:%llu\n#EXT-X-MAP:URI=\"/video/init?token=%s\"\n",target,(unsigned long long)first_group,token);
    for(group=first_group;group<=last_group;++group) {
        first=group*HLS_GROUP+1;last=first+HLS_GROUP-1;
        duration=(parts[last%PART_COUNT].pts+33333-parts[first%PART_COUNT].pts)/1000000.0;
        used+=(size_t)snprintf(text+used,sizeof(text)-used,"#EXTINF:%.6f,\n/video/hls/%u/%llu/%llu?token=%s\n",duration,state.generation,(unsigned long long)first,(unsigned long long)last,token);
    }
    if(used<sizeof(text)){*out=malloc(used+1);if(*out)memcpy(*out,text,used+1);else used=0;}else used=0;
 done:
    pthread_mutex_unlock(&lock);return *out?used:0;
}
size_t airtizen_video_hls_segment(unsigned generation,uint64_t first,uint64_t last,uint8_t **out) {
    uint64_t i;size_t size=0,offset=0;
    *out=NULL;
    if(!first || last<first || last-first!=HLS_GROUP-1 || (first-1)%HLS_GROUP)return 0;
    pthread_mutex_lock(&lock);
    if(generation!=state.generation || last>state.latest)goto done;
    for(i=first;i<=last;++i) {
        struct part *p=parts+i%PART_COUNT;
        if(p->seq!=i || !p->data || p->size>TOTAL_LIMIT-size){size=0;goto done;}
        size+=p->size;
    }
    *out=malloc(size);if(!*out){size=0;goto done;}
    for(i=first;i<=last;++i) {
        struct part *p=parts+i%PART_COUNT;memcpy(*out+offset,p->data,p->size);offset+=p->size;
    }
 done:
    pthread_mutex_unlock(&lock);return size;
}
