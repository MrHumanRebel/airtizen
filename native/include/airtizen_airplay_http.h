/* HTTP video controls served on the receiver's AirPlay port. Included from raop.c. */
#include "airtizen_playback.h"
#include "airtizen_photo.h"
#include "airtizen_video.h"
#include <math.h>
static void airtizen_http_reply(http_response_t **response,int code,const char *type,const char *data,int size){
    *response=http_response_init("HTTP/1.1",code,code==200?"OK":code==404?"Not Found":code==501?"Not Implemented":code==412?"Precondition Failed":code==413?"Content Too Large":code==503?"Service Unavailable":"Bad Request");
    if(type)http_response_add_header(*response,"Content-Type",type);
    if(!size)http_response_add_header(*response,"Content-Length","0");
    http_response_finish(*response,data,size);
}
static int airtizen_airplay_http(http_request_t *request,http_response_t **response){
    const char *method=http_request_get_method(request),*url=http_request_get_url(request);
    const char *data;int length;airtizen_playback_state playback;
    if(!method || !url)return 0;
    data=http_request_get_data(request,&length);
    if(!strcmp(method,"POST") && !strcmp(url,"/reverse")){airtizen_http_reply(response,501,NULL,NULL,0);return 1;}
    if(!strcmp(method,"GET") && !strcmp(url,"/server-info")){
        char receiver[96],identity[32],*encoded=NULL;uint32_t size=0;plist_t info=plist_new_dict();
        airtizen_mdns_identity(receiver,sizeof(receiver),identity,sizeof(identity));
        plist_dict_set_item(info,"deviceid",plist_new_string(identity));
        plist_dict_set_item(info,"features",plist_new_uint(AIRTIZEN_FEATURES));
        plist_dict_set_item(info,"model",plist_new_string("AirTizen1,1"));
        plist_dict_set_item(info,"protovers",plist_new_string("1.0"));
        plist_dict_set_item(info,"srcvers",plist_new_string("220.68"));
        plist_to_xml(info,&encoded,&size);plist_free(info);
        airtizen_http_reply(response,200,"text/x-apple-plist+xml",encoded,(int)size);free(encoded);return 1;
    }
    if(!strcmp(method,"PUT") && !strcmp(url,"/photo")){
        const char *action=http_request_get_header(request,"X-Apple-AssetAction");
        int result=airtizen_photo_put(action,http_request_get_header(request,"X-Apple-AssetKey"),http_request_get_header(request,"X-Apple-Session-ID"),http_request_get_header(request,"X-Apple-Transition"),data,(size_t)length);
        if(result==200 && (!action || strcmp(action,"cacheOnly"))){airtizen_playback_stop();airtizen_video_reset();}
        airtizen_http_reply(response,result,NULL,NULL,0);return 1;
    }
    if(!strcmp(method,"POST") && !strcmp(url,"/play")){
        plist_t root=NULL;char *location=NULL;double fraction=0;int result=-1;
        if(data && length>=40 && length<=8192 && !memcmp(data,"bplist00",8)){
            plist_t position;
            plist_from_bin(data,(uint32_t)length,&root);
            if(root && plist_get_node_type(root)==PLIST_DICT){
                plist_get_string_val(plist_dict_get_item(root,"Content-Location"),&location);
                position=plist_dict_get_item(root,"Start-Position");
                if(position && plist_get_node_type(position)==PLIST_REAL)plist_get_real_val(position,&fraction);
                else if(position && plist_get_node_type(position)==PLIST_UINT){uint64_t value=0;plist_get_uint_val(position,&value);fraction=(double)value;}
                else if(position)fraction=NAN;
                result=airtizen_playback_play(location,fraction);
            }
            free(location);plist_free(root);
        } else if(data && length>0 && length<=8192 && !memchr(data,0,(size_t)length)){
            char *copy=malloc((size_t)length+1),*line,*save=NULL;
            if(copy){
                memcpy(copy,data,(size_t)length);copy[length]=0;
                for(line=strtok_r(copy,"\r\n",&save);line;line=strtok_r(NULL,"\r\n",&save)){
                    if(!strncasecmp(line,"Content-Location:",17)){
                        const char *value=line+17;while(*value==' ')++value;
                        if(location){free(location);location=NULL;fraction=NAN;break;}
                        location=strdup(value);
                    } else if(!strncasecmp(line,"Start-Position:",15)){
                        char *end;const char *value=line+15;while(*value==' ')++value;
                        fraction=strtod(value,&end);if(end==value || *end)fraction=NAN;
                    }
                }
                result=airtizen_playback_play(location,fraction);free(location);free(copy);
            }
        }
        if(result==0)airtizen_photo_reset();
        airtizen_http_reply(response,result==0?200:400,NULL,NULL,0);return 1;
    }
    if(!strcmp(method,"POST") && !strcmp(url,"/stop")){airtizen_playback_stop();airtizen_photo_reset();airtizen_http_reply(response,200,NULL,NULL,0);return 1;}
    if(!strcmp(method,"POST") && (!strncmp(url,"/rate?value=",12)||!strncmp(url,"/scrub?position=",16))){
        int seek=!strncmp(url,"/scrub?position=",16);char *end=NULL;const char *value=url+(seek?16:12);
        double number=strtod(value,&end);int result=-1;
        if(end!=value && !*end && isfinite(number))result=seek?airtizen_playback_seek(number):airtizen_playback_rate(number);
        airtizen_http_reply(response,result==0?200:400,NULL,NULL,0);return 1;
    }
    if(!strcmp(method,"GET") && !strcmp(url,"/scrub")){
        char text[160];int size;airtizen_playback_snapshot(&playback);
        size=snprintf(text,sizeof(text),"duration: %.6f\r\nposition: %.6f\r\n",playback.duration,playback.position);
        airtizen_http_reply(response,200,"text/parameters",text,size);return 1;
    }
    if(!strcmp(method,"GET") && !strcmp(url,"/playback-info")){
        plist_t info=plist_new_dict();char *encoded=NULL;uint32_t size=0;
        airtizen_playback_snapshot(&playback);
        plist_dict_set_item(info,"duration",plist_new_real(playback.duration));
        plist_dict_set_item(info,"position",plist_new_real(playback.position));
        plist_dict_set_item(info,"rate",plist_new_real(playback.ready?playback.rate:0));
        plist_dict_set_item(info,"readyToPlay",plist_new_bool(playback.ready));
        plist_dict_set_item(info,"playbackBufferEmpty",plist_new_bool(!playback.ready));
        plist_dict_set_item(info,"playbackBufferFull",plist_new_bool(playback.ready));
        plist_to_xml(info,&encoded,&size);plist_free(info);
        airtizen_http_reply(response,200,"text/x-apple-plist+xml",encoded,(int)size);free(encoded);return 1;
    }
    return 0;
}
