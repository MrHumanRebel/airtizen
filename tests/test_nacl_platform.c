#include "airtizen_mdns.h"
#include "raop_rtp.h"
#include "byteutils.h"
#include <arpa/inet.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
int main(void) {
    struct timespec before,after;uint64_t measured;
    char name[96],id[32];
    logger_t *logger;raop_rtp_t *rtp;raop_callbacks_t callbacks={0};
    unsigned char remote[4]={127,0,0,1},key[16]={0},iv[16]={0},secret[32]={0},packet[128];
    struct sockaddr_in address={0};socklen_t size=sizeof(address);int peer;fd_set ready;struct timeval timeout={2,0};
    unsigned short control=0,timing=0,data=0;
    clock_gettime(CLOCK_MONOTONIC,&before);measured=now_us();clock_gettime(CLOCK_MONOTONIC,&after);
    assert(measured>=(uint64_t)before.tv_sec*1000000ULL+before.tv_nsec/1000);
    assert(measured<=(uint64_t)after.tv_sec*1000000ULL+after.tv_nsec/1000);
    assert(airtizen_mdns_set_network("127.0.0.1","020000000001")<0);
    assert(airtizen_mdns_set_network("224.0.0.251","020000000001")<0);
    assert(airtizen_mdns_set_network("0.0.0.0","020000000001")<0);
    assert(airtizen_mdns_set_network("192.0.2.20","bad")<0);
    assert(airtizen_mdns_set_network("192.0.2.20","020000000001")==0);
    airtizen_mdns_identity(name,sizeof(name),id,sizeof(id));assert(!strcmp(id,"02:00:00:00:00:01"));
    peer=socket(AF_INET,SOCK_DGRAM,0);assert(peer>=0);
    address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    assert(bind(peer,(struct sockaddr*)&address,sizeof(address))==0);
    assert(getsockname(peer,(struct sockaddr*)&address,&size)==0);
    logger=logger_init();assert(logger);
    rtp=raop_rtp_init(logger,&callbacks,remote,4,"silent sender","test",key,iv,secret,ntohs(address.sin_port));assert(rtp);
    assert(raop_rtp_configure_audio(rtp,8,480,44100,2)==0);
    raop_rtp_start_audio(rtp,1,0,ntohs(address.sin_port),&control,&timing,&data);
    assert(control && timing && data);
    FD_ZERO(&ready);FD_SET(peer,&ready);assert(select(peer+1,&ready,NULL,NULL,&timeout)==1);
    assert(recv(peer,packet,sizeof(packet),0)==32);
    /* Never reply to timing: TEARDOWN must still return and join its workers. */
    measured=now_us();raop_rtp_stop(rtp);assert(now_us()-measured<2000000ULL);
    raop_rtp_destroy(rtp);logger_destroy(logger);close(peer);return 0;
}
