#ifndef AIRTIZEN_AUDIO_PARAMETERS_H
#define AIRTIZEN_AUDIO_PARAMETERS_H
/* Protocol format bits are documented by openairplay/airplay2-receiver
 * AirplayAudFmt. Support exactly the sample formats implemented here. */
static int airtizen_audio_parameters(plist_t stream,unsigned *ct_out,unsigned *frames_out,unsigned *rate_out,unsigned *channels_out){
    uint64_t ct=8,frames,rate=44100,channels=2,format=0;
    plist_t node=plist_dict_get_item(stream,"ct");int explicit_ct=node!=NULL;
    if(node){if(plist_get_node_type(node)!=PLIST_UINT)return -1;plist_get_uint_val(node,&ct);}
    node=plist_dict_get_item(stream,"audioFormat");
    if(node){
        unsigned format_ct=0;
        if(plist_get_node_type(node)!=PLIST_UINT)return -1;plist_get_uint_val(node,&format);
        switch(format){
            case 1ULL<<18:format_ct=2;break;
            case 1ULL<<20:format_ct=2;rate=48000;break;
            case 1ULL<<22:format_ct=4;break;
            case 1ULL<<23:format_ct=4;rate=48000;break;
            case 1ULL<<24:format_ct=8;break;
            default:return -1;
        }
        if(explicit_ct && ct!=format_ct)return -1;ct=format_ct;
    }
    frames=ct==2?352:ct==4?1024:480;
    node=plist_dict_get_item(stream,"spf");if(node){if(plist_get_node_type(node)!=PLIST_UINT)return -1;plist_get_uint_val(node,&frames);}
    node=plist_dict_get_item(stream,"sr");if(node){uint64_t requested=rate;if(plist_get_node_type(node)!=PLIST_UINT)return -1;plist_get_uint_val(node,&requested);if(format && requested!=rate)return -1;rate=requested;}
    node=plist_dict_get_item(stream,"ch");if(node){if(plist_get_node_type(node)!=PLIST_UINT)return -1;plist_get_uint_val(node,&channels);if(format && channels!=2)return -1;}
    if((ct!=2 && ct!=4 && ct!=8) || !frames || frames>4096 || (rate!=44100 && rate!=48000) || !channels || channels>2)return -1;
    if(ct==8 && (frames!=480 || rate!=44100 || channels!=2))return -1;
    if(ct==4 && (frames!=1024 || channels!=2))return -1;
    *ct_out=(unsigned)ct;*frames_out=(unsigned)frames;*rate_out=(unsigned)rate;*channels_out=(unsigned)channels;return 0;
}
#endif
