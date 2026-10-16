#include "airtizen_photo.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#define SLOTS 4
#define CACHE_BYTES (16U*1024U*1024U)
typedef struct {uint8_t *data;size_t size;unsigned width,height;uint64_t age;char key[129],session[65],mime[16];} asset;
static asset cache[SLOTS];
static airtizen_photo_state state;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static int displayed=-1;
static uint64_t age;
static unsigned session_counter;
static unsigned be16(const uint8_t *p){return ((unsigned)p[0]<<8)|p[1];}
static unsigned be32(const uint8_t *p){return ((unsigned)p[0]<<24)|((unsigned)p[1]<<16)|((unsigned)p[2]<<8)|p[3];}
static int identifier(const char *s,size_t max){size_t i;if(!s || strlen(s)>max)return 0;for(i=0;s[i];++i)if((unsigned char)s[i]<33 || (unsigned char)s[i]>126)return 0;return 1;}
/* Bound decoded pixels before the TV browser receives compressed data. The browser
 * remains responsible for full JPEG/PNG decoding and reports failures separately. */
static int dimensions(const uint8_t *p,size_t n,unsigned *w,unsigned *h,char mime[16]) {
    if(n>=33 && !memcmp(p,"\x89PNG\r\n\x1a\n",8) && be32(p+8)==13 && !memcmp(p+12,"IHDR",4)) {
        *w=be32(p+16);*h=be32(p+20);strcpy(mime,"image/png");
    } else if(n>=4 && p[0]==255 && p[1]==216) {
        size_t at=2;int found=0;strcpy(mime,"image/jpeg");
        while(at<n){unsigned marker,length;
            if(p[at++]!=255)return -1;while(at<n && p[at]==255)++at;if(at==n)return -1;marker=p[at++];
            if(marker==217 || marker==218)break;
            if(marker==1 || (marker>=208 && marker<=215))continue;
            if(at+2>n)return -1;length=be16(p+at);if(length<2 || length>n-at)return -1;
            if(marker==192 || marker==193 || marker==194){if(length<8 || p[at+2]!=8)return -1;*h=be16(p+at+3);*w=be16(p+at+5);found=1;break;}
            at+=length;
        }
        if(!found)return -1;
    } else return -1;
    return !*w || !*h || *w>8192 || *h>8192 || (uint64_t)(*w)*(*h)>16000000 ? -1:0;
}
static void discard(int slot){free(cache[slot].data);memset(&cache[slot],0,sizeof(cache[slot]));}
static int oldest(int except){int i,result=-1;for(i=0;i<SLOTS;++i)if(i!=except && i!=displayed && cache[i].data && (result<0 || cache[i].age<cache[result].age))result=i;return result;}
int airtizen_photo_put(const char *action,const char *key,const char *session,const char *transition,const void *data,size_t size){
    int i,slot=-1,caching=action && !strcmp(action,"cacheOnly"),cached=action && !strcmp(action,"displayCached");
    unsigned w=0,h=0;char mime[16];uint8_t *copy=NULL;size_t total;
    key=key?key:"";session=session?session:"";
    if(!identifier(key,128) || !identifier(session,64) || (action && *action && !caching && !cached) || ((cached || caching) && !*key))return 400;
    if(transition && *transition && strcmp(transition,"None") && strcmp(transition,"Dissolve"))return 400;
    if(cached){if(size)return 400;}
    else {
        if(size>AIRTIZEN_PHOTO_MAX_BYTES)return 413;
        if(!data || dimensions(data,size,&w,&h,mime))return 400;
        copy=malloc(size);if(!copy)return 503;memcpy(copy,data,size);
    }
    pthread_mutex_lock(&lock);
    for(i=0;i<SLOTS;++i)if(cache[i].data && !strcmp(cache[i].key,key) && !strcmp(cache[i].session,session)){slot=i;break;}
    if(cached && slot<0){pthread_mutex_unlock(&lock);return 412;}
    /* Re-caching the active key must not mutate the image already on screen. */
    if(caching && slot>=0 && slot==displayed){pthread_mutex_unlock(&lock);free(copy);return 200;}
    if(!cached){
        if(slot<0)for(i=0;i<SLOTS;++i)if(!cache[i].data){slot=i;break;}
        if(slot<0)slot=oldest(-1);
        if(slot<0){pthread_mutex_unlock(&lock);free(copy);return 503;}
        total=size;for(i=0;i<SLOTS;++i)if(i!=slot)total+=cache[i].size;
        while(total>CACHE_BYTES){int victim=oldest(slot);if(victim<0){pthread_mutex_unlock(&lock);free(copy);return 503;}total-=cache[victim].size;discard(victim);}
        discard(slot);cache[slot].data=copy;cache[slot].size=size;cache[slot].width=w;cache[slot].height=h;
        strcpy(cache[slot].mime,mime);strcpy(cache[slot].key,key);strcpy(cache[slot].session,session);
    }
    cache[slot].age=++age;
    if(!caching){if(!state.active || strcmp(state.session,session)){if(!++session_counter)++session_counter;state.session_id=session_counter;}displayed=slot;++state.generation;state.active=1;state.ready=state.failed=0;state.width=cache[slot].width;state.height=cache[slot].height;state.dissolve=transition && !strcmp(transition,"Dissolve");strcpy(state.session,session);}
    pthread_mutex_unlock(&lock);return 200;
}
void airtizen_photo_reset(void){unsigned next;int i;pthread_mutex_lock(&lock);for(i=0;i<SLOTS;++i)discard(i);displayed=-1;next=state.generation+1;memset(&state,0,sizeof(state));state.generation=next;pthread_mutex_unlock(&lock);}
void airtizen_photo_snapshot(airtizen_photo_state *out){pthread_mutex_lock(&lock);*out=state;pthread_mutex_unlock(&lock);}
size_t airtizen_photo_copy(unsigned generation,uint8_t **out,char mime[16]){size_t size=0;*out=NULL;pthread_mutex_lock(&lock);if(state.active && state.generation==generation && displayed>=0){asset *a=&cache[displayed];*out=malloc(a->size);if(*out){memcpy(*out,a->data,a->size);size=a->size;strcpy(mime,a->mime);}}pthread_mutex_unlock(&lock);return size;}
int airtizen_photo_report(unsigned generation,int failed){int result=-1;pthread_mutex_lock(&lock);if(state.active && state.generation==generation){state.failed=!!failed;state.ready=!failed;result=0;}pthread_mutex_unlock(&lock);return result;}
