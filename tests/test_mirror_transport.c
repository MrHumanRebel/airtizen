#include "raop_rtp_mirror.h"
#include <arpa/inet.h>
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static unsigned frames,configs;
static const unsigned char cipher[]={0x27,0x24,0xd6,0x43,0x1f,0x11,0x84,0x00,0x60,0x85,0xe0,0x3f,0x9c,0x00,0x02,0x06,0x44,0x68};
static const unsigned char plain[]={0,0,0,1,0x65,0x11,0,0,0,1,0x41,0,0,0,1,0x65,0x22,0x33};
static const size_t lengths[]={6,5,7},offsets[]={0,6,11};
static void receive_frame(void *cls,h264_decode_struct *frame,const char *name,const char *id) {
    (void)cls;(void)name;(void)id;
    pthread_mutex_lock(&lock);
    if(frame->frame_type==0){assert(frame->data_len>8);++configs;}
    else {
        assert(frames<3 && frame->data_len==(int)lengths[frames]);
        assert(memcmp(frame->data,plain+offsets[frames],lengths[frames])==0);
        assert(frame->pts>=(uint64_t)frames*33333 && frame->pts<=(uint64_t)frames*33334);
        ++frames;
    }
    pthread_mutex_unlock(&lock);
}
static void send_all(int fd,const unsigned char *data,size_t size){while(size){ssize_t n=send(fd,data,size,0);assert(n>0);data+=n;size-=(size_t)n;}}
static void header(unsigned char *out,unsigned size,unsigned type,uint64_t ntp){unsigned i;memset(out,0,128);for(i=0;i<4;++i)out[i]=(unsigned char)(size>>(8*i));out[4]=(unsigned char)type;for(i=0;i<8;++i)out[8+i]=(unsigned char)(ntp>>(8*i));}
int main(void) {
    logger_t *logger=logger_init();raop_callbacks_t callbacks={0};
    unsigned char remote[]={127,0,0,1},key[16]={0},secret[32]={0},packet[128];
    /* Minimal well-bounded AVC configuration, used only to test transport parsing. */
    unsigned char config[]={1,0x42,0,0x1e,0xff,0xe1,0,4,0x67,0x42,0,0x1e,1,0,2,0x68,0};
    raop_rtp_mirror_t *receiver;unsigned short timing=0,port=0;
    struct sockaddr_in address;int fd;unsigned i;
    callbacks.video_process=receive_frame;
    receiver=raop_rtp_mirror_init(logger,&callbacks,remote,4,"Fixture","fixture",key,secret,9);assert(receiver);
    raop_rtp_init_mirror_aes(receiver,123);raop_rtp_start_mirror(receiver,0,9,&timing,&port);assert(port);
    fd=socket(AF_INET,SOCK_STREAM,0);assert(fd>=0);memset(&address,0,sizeof(address));address.sin_family=AF_INET;address.sin_port=htons(port);address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    assert(connect(fd,(struct sockaddr *)&address,sizeof(address))==0);
    header(packet,sizeof(config),1,0);
    send_all(fd,packet,7);send_all(fd,packet+7,121);send_all(fd,config,sizeof(config));
    for(i=0;i<3;++i){header(packet,(unsigned)lengths[i],0,(1ULL<<32)+(((uint64_t)i<<32)/30));send_all(fd,packet,128);send_all(fd,cipher+offsets[i],lengths[i]);}
    for(i=0;i<100;++i){unsigned observed;pthread_mutex_lock(&lock);observed=frames;pthread_mutex_unlock(&lock);if(observed==3)break;usleep(10000);}
    pthread_mutex_lock(&lock);assert(frames==3 && configs==1);pthread_mutex_unlock(&lock);
    /* Keep a partial header open: stopping must interrupt the receiver read loop. */
    header(packet,100,0,0);send_all(fd,packet,64);
    raop_rtp_mirror_stop(receiver);close(fd);raop_rtp_mirror_destroy(receiver);logger_destroy(logger);
    puts("Independent AES-CTR fixture survives 6/5/7-byte mirror packets, fragmented headers and stop during partial input");
    return 0;
}
