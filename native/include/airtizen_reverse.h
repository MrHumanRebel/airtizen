/* Bounded PTTH playback event channel; included with the private RAOP connection type. */
#include <time.h>
#include "byteutils.h"
static uint64_t airtizen_event_clock(void){return now_us()/1000000ULL;}
static int airtizen_reverse_upgrade(raop_conn_t *conn,http_request_t *request,http_response_t **response){
    const char *method=http_request_get_method(request),*url=http_request_get_url(request),*session;
    airtizen_playback_state playback;
    if(!method || !url || strcmp(method,"POST") || strcmp(url,"/reverse"))return 0;
    session=http_request_get_header(request,"X-Apple-Session-ID");
    if(session){size_t i,length=strlen(session);if(length>64)return 0;for(i=0;i<length;++i)if(!isalnum((unsigned char)session[i]) && session[i]!='-')return 0;snprintf(conn->event_session,sizeof(conn->event_session),"%s",session);}
    *response=http_response_init("HTTP/1.1",101,"Switching Protocols");
    http_response_add_header(*response,"Upgrade","PTTH/1.0");http_response_add_header(*response,"Connection","Upgrade");
    http_response_add_header(*response,"Content-Length","0");http_response_finish(*response,NULL,0);
    airtizen_playback_snapshot(&playback);conn->reverse=1;conn->event_generation=playback.generation;conn->event_state=playback.active?-1:0;
    return 1;
}
static int airtizen_reverse_poll(void *opaque,http_response_t **event){
    raop_conn_t *conn=opaque;airtizen_playback_state playback;int state;const char *name;
    plist_t info;char *encoded=NULL;uint32_t length=0;
    if(conn->reverse<0)return -1;if(!conn->reverse)return 0;
    if(conn->event_waiting)return airtizen_event_clock()-conn->event_time>10?-1:0;
    airtizen_playback_snapshot(&playback);
    state=!playback.active || playback.failed?0:!playback.ready?1:playback.rate==0?2:3;
    if(state==conn->event_state && playback.generation==conn->event_generation)return 0;
    name=state==0?"stopped":state==1?"loading":state==2?"paused":"playing";
    info=plist_new_dict();plist_dict_set_item(info,"category",plist_new_string("video"));plist_dict_set_item(info,"state",plist_new_string(name));
    plist_to_xml(info,&encoded,&length);plist_free(info);
    *event=http_response_init_request("POST","/event");if(!*event){free(encoded);return -1;}
    http_response_add_header(*event,"Content-Type","text/x-apple-plist+xml");
    if(conn->event_session[0])http_response_add_header(*event,"X-Apple-Session-ID",conn->event_session);
    http_response_finish(*event,encoded,(int)length);free(encoded);
    conn->event_state=state;conn->event_generation=playback.generation;conn->event_waiting=1;conn->event_time=airtizen_event_clock();return 1;
}
