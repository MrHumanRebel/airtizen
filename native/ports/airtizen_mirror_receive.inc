/* AirTizen replacement for the donor's unchecked, blocking mirror payload loop.
 * Included after raop_rtp_mirror_t is defined in the generated source. */
#include "airtizen_h264.h"
static int airtizen_mirror_read(raop_rtp_mirror_t *receiver,int fd,unsigned char *out,size_t size) {
    size_t offset=0;
    while(offset<size) {
        fd_set fds;struct timeval timeout={0,250000};int active,rc;ssize_t count;
        MUTEX_LOCK(receiver->run_mutex);active=receiver->running;MUTEX_UNLOCK(receiver->run_mutex);
        if(!active)return -1;
        FD_ZERO(&fds);FD_SET(fd,&fds);
        rc=select(fd+1,&fds,NULL,NULL,&timeout);
        if(rc<0) { if(errno==EINTR)continue;return -1; }
        if(!rc)continue;
        count=recv(fd,out+offset,size-offset,0);
        if(count<=0) { if(count<0 && errno==EINTR)continue;return -1; }
        offset+=(size_t)count;
    }
    return 0;
}
static THREAD_RETVAL raop_rtp_mirror_thread(void *arg) {
    raop_rtp_mirror_t *receiver=arg;
    int client=-1;
    uint64_t base=0;
    int have_base=0;
    for(;;) {
        unsigned char header[128],*payload=NULL,*decoded=NULL;
        uint32_t size;unsigned type;int active;
        MUTEX_LOCK(receiver->run_mutex);active=receiver->running;MUTEX_UNLOCK(receiver->run_mutex);
        if(!active)break;
        if(client<0) {
            fd_set fds;struct timeval timeout={0,250000};int rc;
            FD_ZERO(&fds);FD_SET(receiver->mirror_data_sock,&fds);
            rc=select(receiver->mirror_data_sock+1,&fds,NULL,NULL,&timeout);
            if(rc<0) { if(errno==EINTR)continue;break; }
            if(!rc)continue;
            client=accept(receiver->mirror_data_sock,NULL,NULL);
            if(client<0)break;
        }
        if(airtizen_mirror_read(receiver,client,header,sizeof(header))<0)break;
        size=(uint32_t)byteutils_get_int(header,0);type=(unsigned)byteutils_get_short(header,4)&255;
        if(size>2U*1024U*1024U || (type==1 && size>65536))break;
        if(!size)continue;
        payload=malloc(size);
        if(!payload)break;
        if(airtizen_mirror_read(receiver,client,payload,size)<0) { free(payload);break; }
        if(type==0) {
            uint64_t ntp=byteutils_get_long(header,8);
            uint64_t micros=(ntp>>32)*1000000ULL+(((ntp&0xffffffffULL)*1000000ULL)>>32);
            h264_decode_struct frame;
            decoded=malloc(size);
            if(!decoded) { free(payload);break; }
            mirror_buffer_decrypt(receiver->buffer,payload,decoded,(int)size);
            if(airtizen_h264_frame(decoded,size)<0) { free(decoded);free(payload);break; }
            if(!have_base) { base=micros;have_base=1; }
            if(micros<base) { free(decoded);free(payload);break; }
            memset(&frame,0,sizeof(frame));frame.data=decoded;frame.data_len=(int)size;frame.frame_type=1;frame.pts=micros-base;
            if(receiver->callbacks.video_process)receiver->callbacks.video_process(receiver->callbacks.cls,&frame,receiver->remoteName,receiver->remoteDeviceId);
        } else if(type==1) {
            size_t length;
            h264_decode_struct frame;
            decoded=malloc(size+2048);
            if(!decoded) { free(payload);break; }
            length=airtizen_h264_config(payload,size,decoded,size+2048);
            if(!length) { free(decoded);free(payload);break; }
            memset(&frame,0,sizeof(frame));frame.data=decoded;frame.data_len=(int)length;
            if(receiver->callbacks.video_process)receiver->callbacks.video_process(receiver->callbacks.cls,&frame,receiver->remoteName,receiver->remoteDeviceId);
        }
        free(decoded);free(payload);
    }
    if(client>=0)closesocket(client);
    return 0;
}
