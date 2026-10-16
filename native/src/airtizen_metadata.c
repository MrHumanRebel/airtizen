#include "airtizen_metadata.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#define ART_LIMIT (512U*1024U)
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static airtizen_metadata_state state;
static uint8_t *artwork;static size_t artwork_size;static char artwork_mime[16];
static int parse(const uint8_t *data,size_t size,unsigned depth,airtizen_metadata_state *next){
    size_t position=0;
    if(depth>8)return -1;
    while(position<size){
        uint32_t length;char *out=NULL;const uint8_t *tag;
        if(size-position<8)return -1;
        tag=data+position;length=((uint32_t)tag[4]<<24)|((uint32_t)tag[5]<<16)|((uint32_t)tag[6]<<8)|tag[7];position+=8;
        if(length>size-position)return -1;
        if(!memcmp(tag,"mlit",4)||!memcmp(tag,"mdcl",4)){if(parse(data+position,length,depth+1,next)<0)return -1;}
        else {
            if(!memcmp(tag,"minm",4))out=next->title;
            if(!memcmp(tag,"asar",4))out=next->artist;
            if(!memcmp(tag,"asal",4))out=next->album;
            if(out){size_t copied=length>255?255:length;memcpy(out,data+position,copied);out[copied]=0;}
        }
        position+=length;
    }
    return 0;
}
int airtizen_metadata_parse(const void *data,size_t size){
    airtizen_metadata_state next;
    if(!data || !size || size>65536)return -1;
    memset(&next,0,sizeof(next));
    if(parse(data,size,0,&next)<0)return -1;
    pthread_mutex_lock(&lock);memcpy(state.title,next.title,sizeof(state.title));memcpy(state.artist,next.artist,sizeof(state.artist));memcpy(state.album,next.album,sizeof(state.album));pthread_mutex_unlock(&lock);return 0;
}
int airtizen_metadata_artwork(const void *data,size_t size){
    const uint8_t *bytes=data;const char *mime;uint8_t *copy;
    if(!data || size<8 || size>ART_LIMIT)return -1;
    if(!memcmp(data,"\x89PNG\r\n\x1a\n",8))mime="image/png";
    else if(bytes[0]==0xff && bytes[1]==0xd8 && bytes[2]==0xff)mime="image/jpeg";
    else return -1;
    copy=malloc(size);if(!copy)return -1;memcpy(copy,data,size);
    pthread_mutex_lock(&lock);free(artwork);artwork=copy;artwork_size=size;memcpy(artwork_mime,mime,strlen(mime)+1);++state.artwork;pthread_mutex_unlock(&lock);return 0;
}
void airtizen_metadata_progress(uint32_t start,uint32_t current,uint32_t end){
    uint32_t total=end-start,progress=current-start;
    if(total>0x7fffffffU || progress>total)return;
    pthread_mutex_lock(&lock);state.progress=progress;state.total=total;pthread_mutex_unlock(&lock);
}
void airtizen_metadata_reset(void){unsigned version;pthread_mutex_lock(&lock);version=state.artwork+1;memset(&state,0,sizeof(state));state.artwork=version;free(artwork);artwork=NULL;artwork_size=0;pthread_mutex_unlock(&lock);}
void airtizen_metadata_snapshot(airtizen_metadata_state *out){pthread_mutex_lock(&lock);*out=state;if(!artwork)out->artwork=0;pthread_mutex_unlock(&lock);}
size_t airtizen_metadata_copy_artwork(unsigned version,uint8_t **data,char mime[16]){
    size_t size=0;*data=NULL;pthread_mutex_lock(&lock);
    if(version==state.artwork && artwork_size){*data=malloc(artwork_size);if(*data){memcpy(*data,artwork,artwork_size);memcpy(mime,artwork_mime,16);size=artwork_size;}}
    pthread_mutex_unlock(&lock);return size;
}
