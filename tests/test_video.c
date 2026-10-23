#include "airtizen_video.h"
#include "airtizen_h264.h"
#include "airtizen_runtime.h"
#include "airtizen_core.h"
#include "airtizen_control_http.h"
#include <libavcodec/avcodec.h>
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static volatile sig_atomic_t done;
static void finish(int sig){(void)sig;done=1;}
struct packet {uint8_t *data;size_t size;};
int main(int argc,char **argv) {
    AVCodecParserContext *parser=av_parser_init(AV_CODEC_ID_H264);
    AVCodecContext *codec=avcodec_alloc_context3(NULL);
    uint8_t *raw,*cursor,*config=NULL;size_t size,left,config_size=0;
    struct packet packets[64];unsigned count=0,i;FILE *file;
    char path[1024];airtizen_video_state state;
    int serve=argc>1 && !strcmp(argv[1],"--serve");
    snprintf(path,sizeof(path),"%s/mirror-320x180.h264",AIRTIZEN_FIXTURE_DIR);
    file=fopen(path,"rb");assert(file);fseek(file,0,SEEK_END);size=(size_t)ftell(file);rewind(file);
    raw=av_mallocz(size+AV_INPUT_BUFFER_PADDING_SIZE);assert(raw);assert(fread(raw,1,size,file)==size);fclose(file);
    cursor=raw;left=size;
    while(left) {
        uint8_t *output=NULL;int output_size=0;
        int used=av_parser_parse2(parser,codec,&output,&output_size,cursor,(int)left,AV_NOPTS_VALUE,AV_NOPTS_VALUE,0);
        assert(used>=0 && (used || output_size));cursor+=used;left-=(size_t)used;
        if(output_size){assert(count<64);packets[count].data=av_memdup(output,(size_t)output_size);packets[count++].size=(size_t)output_size;}
    }
    {
        uint8_t *output=NULL;int output_size=0;
        av_parser_parse2(parser,codec,&output,&output_size,NULL,0,AV_NOPTS_VALUE,AV_NOPTS_VALUE,0);
        if(output_size){assert(count<64);packets[count].data=av_memdup(output,(size_t)output_size);packets[count++].size=(size_t)output_size;}
    }
    av_parser_close(parser);avcodec_free_context(&codec);av_free(raw);assert(count==30);
    /* Extract only SPS/PPS from the first Annex-B access unit. */
    config=av_mallocz(packets[0].size+AV_INPUT_BUFFER_PADDING_SIZE);assert(config);
    for(i=0;i+4<packets[0].size;) {
        size_t prefix=0,start,end;
        if(!packets[0].data[i] && !packets[0].data[i+1] && packets[0].data[i+2]==1)prefix=3;
        else if(!packets[0].data[i] && !packets[0].data[i+1] && !packets[0].data[i+2] && packets[0].data[i+3]==1)prefix=4;
        if(!prefix){++i;continue;}
        start=i+prefix;end=start+1;
        while(end+3<packets[0].size && !(packets[0].data[end]==0 && packets[0].data[end+1]==0 && (packets[0].data[end+2]==1 || (packets[0].data[end+2]==0 && packets[0].data[end+3]==1))))++end;
        if(end+3>=packets[0].size)end=packets[0].size;
        if((packets[0].data[start]&31)==7 || (packets[0].data[start]&31)==8){memcpy(config+config_size,"\0\0\0\1",4);config_size+=4;memcpy(config+config_size,packets[0].data+start,end-start);config_size+=end-start;}
        i=(unsigned)end;
    }
    assert(config_size>8 && airtizen_video_configure(config,config_size)==0);
    if(serve) {
        const char *token=getenv("AIRTIZEN_CONTROL_TOKEN");
        if(token)assert(airtizen_control_http_set_token(token)==0);
        assert(airtizen_runtime_start("AirTizen TV")==0);
        assert(airtizen_control_http_start(45110)==0);
        signal(SIGTERM,finish);signal(SIGINT,finish);
        for(i=0;!done;++i){
            airtizen_core_state receiver;airtizen_core_snapshot(&receiver);
            if(receiver.running) {
                /* Discovery restoration can restart the receiver. Simulate the
                 * new sender's configuration instead of feeding an orphaned
                 * stream into a cleared muxer after that restart. */
                airtizen_video_snapshot(&state);
                if(!state.mime[0]){assert(airtizen_video_configure(config,config_size)==0);i=0;}
                (void)airtizen_video_frame(packets[i%count].data,packets[i%count].size,(uint64_t)i*33333);
            }
            usleep(33333);
        }
        airtizen_control_http_stop();airtizen_runtime_stop();
    } else {
        uint8_t *data=NULL;unsigned generation;
        FILE *output=fopen("mirror-smoke.mp4","wb");assert(output);
        for(i=0;i<count;++i)assert(airtizen_video_frame(packets[i].data,packets[i].size,(uint64_t)i*33333)==1);
        airtizen_video_snapshot(&state);assert(state.available && state.width==320 && state.height==180 && state.latest>=30);
        size=airtizen_video_copy(1,0,&data,&generation);assert(size>32);assert(fwrite(data,1,size,output)==size);free(data);
        for(i=1;i<=count;++i){size=airtizen_video_copy(0,i,&data,&generation);assert(size>0);assert(fwrite(data,1,size,output)==size);free(data);}
        fclose(output);
        {
            char *manifest=NULL;uint8_t *group=NULL;size_t group_size,offset=0;
            assert(airtizen_video_hls_manifest("",&manifest)>0);
            assert(strstr(manifest,"#EXT-X-MAP:URI=\"/video/hls/") && strstr(manifest,"/init\""));
            assert(!strstr(manifest,"token="));free(manifest);
            assert(airtizen_video_hls_manifest("test-control-token",&manifest)>0);
            assert(strstr(manifest,"/init?token=test-control-token\""));free(manifest);
            group_size=airtizen_video_hls_segment(generation,1,15,&group);assert(group_size>0);
            for(i=1;i<=15;++i){
                size=airtizen_video_copy(0,i,&data,&generation);
                assert(size && size<=group_size-offset && !memcmp(group+offset,data,size));offset+=size;free(data);
            }
            assert(offset==group_size);free(group);
            assert(!airtizen_video_hls_segment(generation+1,1,15,&group) && !group);
            assert(!airtizen_video_hls_segment(generation,2,16,&group) && !group);
            assert(!airtizen_video_hls_segment(generation,1,UINT64_MAX,&group) && !group);
        }
        assert(airtizen_video_frame(packets[1].data,packets[1].size,1)<0);
        assert(airtizen_video_copy(0,UINT64_MAX,&data,&generation)==0);
        airtizen_video_reset();airtizen_video_snapshot(&state);assert(!state.available);
        {
            char *manifest=NULL;
            assert(!airtizen_video_hls_manifest("",&manifest) && !manifest);
            assert(!airtizen_video_hls_segment(generation,1,15,&data) && !data);
        }
        { uint8_t invalid[]={0,0,0,0,0x65};assert(airtizen_h264_frame(invalid,sizeof(invalid))<0); }
        { uint8_t valid[]={0,0,0,1,0x65};assert(airtizen_h264_frame(valid,sizeof(valid))==0);assert(valid[3]==1); }
        puts("30 H.264 access units muxed to bounded MP4 fragments; stream restart and invalid input checks passed");
    }
    av_free(config);for(i=0;i<count;++i)av_free(packets[i].data);airtizen_video_reset();return 0;
}
