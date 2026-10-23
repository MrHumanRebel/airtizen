/* Run the actual receiver loop over loopback TCP; crypto is an identity seam.
 * The existing upstream test_mirror_transport.c covers the real AES fixture. */
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include "raop.h"
#include "byteutils.h"
#define THREAD_RETVAL void *
#define MUTEX_LOCK(m) pthread_mutex_lock(&(m))
#define MUTEX_UNLOCK(m) pthread_mutex_unlock(&(m))
#define closesocket close
#define LOGGER_INFO 6
#define logger_log(...) ((void)0)
typedef struct {
 pthread_mutex_t run_mutex;
 int running,mirror_data_sock;
 void *buffer,*logger;
 raop_callbacks_t callbacks;
 const char *remoteName,*remoteDeviceId;
} raop_rtp_mirror_t;
static void mirror_buffer_decrypt(void *buffer,unsigned char *in,unsigned char *out,int n){(void)buffer;memcpy(out,in,(size_t)n);}
#include "airtizen_mirror_receive.inc"
static pthread_mutex_t observed_lock=PTHREAD_MUTEX_INITIALIZER;
static unsigned frames,configs,flushes;
static uint64_t last_pts;
static void video(void *cls,h264_decode_struct *frame,const char *name,const char *id){
 (void)cls;(void)name;(void)id;
 pthread_mutex_lock(&observed_lock);
 if(frame->frame_type){assert(frame->data_len==6 && frame->data[3]==1 && frame->data[4]==0x65);++frames;last_pts=frame->pts;}
 else ++configs;
 pthread_mutex_unlock(&observed_lock);
}
static void flush(void *cls){(void)cls;pthread_mutex_lock(&observed_lock);++flushes;pthread_mutex_unlock(&observed_lock);}
static void wait_for(unsigned f,unsigned c,unsigned x,uint64_t pts){
 unsigned i;int done=0;
 for(i=0;i<200;++i){pthread_mutex_lock(&observed_lock);done=frames==f && configs==c && flushes>=x && last_pts==pts;pthread_mutex_unlock(&observed_lock);if(done)return;usleep(5000);}
 fprintf(stderr,"expected f=%u c=%u flush>=%u pts=%llu; observed %u %u %u %llu\n",f,c,x,(unsigned long long)pts,frames,configs,flushes,(unsigned long long)last_pts);abort();
}
static void send_all(int fd,const unsigned char *data,size_t size){while(size){ssize_t n=send(fd,data,size,0);assert(n>0);data+=n;size-=(size_t)n;}}
static void packet(int fd,unsigned type,uint64_t seconds,const unsigned char *payload,unsigned size){
 unsigned char h[128]={0};unsigned i;uint64_t ntp=seconds<<32;
 for(i=0;i<4;++i)h[i]=(unsigned char)(size>>(8*i));h[4]=(unsigned char)type;
 for(i=0;i<8;++i)h[8+i]=(unsigned char)(ntp>>(8*i));
 send_all(fd,h,7);send_all(fd,h+7,121);send_all(fd,payload,size);
}
static int connect_to(struct sockaddr_in *address){int fd=socket(AF_INET,SOCK_STREAM,0);assert(fd>=0);assert(connect(fd,(struct sockaddr *)address,sizeof(*address))==0);return fd;}
int main(void){
 raop_rtp_mirror_t r={0};pthread_t thread;struct sockaddr_in address={0};socklen_t length=sizeof(address);int fd;
 const unsigned char config[]={1,0x42,0,0x1e,0xff,0xe1,0,4,0x67,0x42,0,0x1e,1,0,2,0x68,0};
 const unsigned char frame[]={0,0,0,2,0x65,0x11};unsigned char partial[128]={0};
 pthread_mutex_init(&r.run_mutex,NULL);r.running=1;r.callbacks.video_process=video;r.callbacks.video_flush=flush;
 r.mirror_data_sock=socket(AF_INET,SOCK_STREAM,0);assert(r.mirror_data_sock>=0);
 address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
 assert(bind(r.mirror_data_sock,(struct sockaddr *)&address,sizeof(address))==0);assert(listen(r.mirror_data_sock,4)==0);assert(getsockname(r.mirror_data_sock,(struct sockaddr *)&address,&length)==0);
 assert(pthread_create(&thread,NULL,raop_rtp_mirror_thread,&r)==0);
 fd=connect_to(&address);packet(fd,1,0,config,sizeof(config));packet(fd,0,100,frame,sizeof(frame));packet(fd,0,101,frame,sizeof(frame));wait_for(2,1,0,1000000);
 /* Clip clock restarts on the same TCP channel, with and without new SPS/PPS. */
 packet(fd,0,1,frame,sizeof(frame));wait_for(3,1,0,0);
 packet(fd,1,0,config,sizeof(config));packet(fd,0,0,frame,sizeof(frame));wait_for(4,2,0,0);
 close(fd);wait_for(4,2,1,0);
 /* A new data connection must work without restarting the RTSP receiver. */
 fd=connect_to(&address);packet(fd,1,0,config,sizeof(config));packet(fd,0,5,frame,sizeof(frame));wait_for(5,3,1,0);
 /* EOF in the middle of a header must also leave the listener usable. */
 send_all(fd,partial,64);close(fd);wait_for(5,3,2,0);
 fd=connect_to(&address);packet(fd,1,0,config,sizeof(config));packet(fd,0,7,frame,sizeof(frame));wait_for(6,4,2,0);
 /* EOF during payload, then another stream. */
 partial[0]=100;send_all(fd,partial,sizeof(partial));send_all(fd,frame,2);close(fd);wait_for(6,4,3,0);
 fd=connect_to(&address);packet(fd,1,0,config,sizeof(config));packet(fd,0,9,frame,sizeof(frame));wait_for(7,5,3,0);
 /* Stop must interrupt a partial read without hanging. */
 send_all(fd,partial,32);pthread_mutex_lock(&r.run_mutex);r.running=0;pthread_mutex_unlock(&r.run_mutex);
 assert(pthread_join(thread,NULL)==0);close(fd);close(r.mirror_data_sock);pthread_mutex_destroy(&r.run_mutex);
 puts("PASS: clip clock reset, TCP reconnect, partial header/payload EOF, and explicit stop");return 0;
}
