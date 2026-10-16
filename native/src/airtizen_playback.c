#include "airtizen_playback.h"
#include <math.h>
#include <pthread.h>
#include <string.h>
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static airtizen_playback_state state;
int airtizen_playback_play(const char *url,double fraction) {
    const char *host,*end,*p;unsigned generation;
    if(!url || strlen(url)>2048 || !isfinite(fraction) || fraction<0 || fraction>1)return -1;
    host=!strncmp(url,"http://",7)?url+7:!strncmp(url,"https://",8)?url+8:NULL;
    if(!host || !*host)return -1;
    end=strpbrk(host,"/?#");if(!end)end=url+strlen(url);
    if(end==host)return -1;
    for(p=url;*p;++p)if((unsigned char)*p<33 || (unsigned char)*p>126 || *p=='"' || *p=='\\' || *p=='<' || *p=='>')return -1;
    for(p=host;p<end;++p)if(*p=='@')return -1;
    pthread_mutex_lock(&lock);generation=state.generation+1;memset(&state,0,sizeof(state));state.generation=generation;
    state.active=1;state.rate=1;state.seek=-1;state.fraction=fraction;memcpy(state.url,url,strlen(url)+1);
    pthread_mutex_unlock(&lock);return 0;
}
void airtizen_playback_stop(void){unsigned generation;pthread_mutex_lock(&lock);generation=state.generation+1;memset(&state,0,sizeof(state));state.generation=generation;state.seek=-1;pthread_mutex_unlock(&lock);}
int airtizen_playback_rate(double rate){if(rate!=0 && rate!=1)return -1;pthread_mutex_lock(&lock);state.rate=rate;++state.command;pthread_mutex_unlock(&lock);return 0;}
int airtizen_playback_seek(double position){if(!isfinite(position) || position<0 || position>604800)return -1;pthread_mutex_lock(&lock);state.seek=position;++state.command;pthread_mutex_unlock(&lock);return 0;}
int airtizen_playback_report(unsigned generation,double position,double duration,int ready,int failed,int ended){
    if(!isfinite(position)||!isfinite(duration)||position<0||duration<0||duration>604800||position>604800)return -1;
    pthread_mutex_lock(&lock);
    if(!state.active || generation!=state.generation){pthread_mutex_unlock(&lock);return -1;}
    state.position=position;state.duration=duration;state.ready=!!ready;state.failed=!!failed;
    if(ended || failed){state.active=0;state.ready=0;state.rate=0;}
    pthread_mutex_unlock(&lock);return 0;
}
void airtizen_playback_snapshot(airtizen_playback_state *out){pthread_mutex_lock(&lock);*out=state;pthread_mutex_unlock(&lock);}
