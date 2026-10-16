/* Locally encode AAC-ELD, encrypt RTP payloads, then exercise the donor decoder. */
#include "raop_buffer.h"
#include "crypto/crypto.h"
#include "ed25519/sha512.h"
#include "aacenc_lib.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void exercise(unsigned ct) {
    const int samples=ct==4?1024:480;
    HANDLE_AACENCODER encoder = NULL;
    AACENC_InfoStruct info = {0};
    unsigned char key[16]={0}, iv[16]={0}, secret[32]={0}, derived[64];
    sha512_context hash;
    logger_t *logger=logger_init();
    raop_buffer_t *buffer;
    raop_callbacks_t cb={0};
    INT_PCM input[2048];
    unsigned char encoded[4096], packet[4108];
    int frame, decoded_frames=0;
    long long energy=0;
    assert(aacEncOpen(&encoder,0,2)==AACENC_OK);
    assert(aacEncoder_SetParam(encoder,AACENC_AOT,ct==4?AOT_AAC_LC:AOT_ER_AAC_ELD)==AACENC_OK);
    assert(aacEncoder_SetParam(encoder,AACENC_SAMPLERATE,44100)==AACENC_OK);
    assert(aacEncoder_SetParam(encoder,AACENC_CHANNELMODE,MODE_2)==AACENC_OK);
    assert(aacEncoder_SetParam(encoder,AACENC_BITRATE,128000)==AACENC_OK);
    assert(aacEncoder_SetParam(encoder,AACENC_TRANSMUX,TT_MP4_RAW)==AACENC_OK);
    assert(aacEncoder_SetParam(encoder,AACENC_GRANULE_LENGTH,samples)==AACENC_OK);
    assert(aacEncEncode(encoder,NULL,NULL,NULL,NULL)==AACENC_OK);
    assert(aacEncInfo(encoder,&info)==AACENC_OK);
    assert(info.frameLength==(unsigned)samples);
    sha512_init(&hash);sha512_update(&hash,key,16);sha512_update(&hash,secret,32);sha512_final(&hash,derived);
    buffer=raop_buffer_init(logger,key,iv,secret);assert(buffer);assert(raop_buffer_configure_audio(buffer,ct,(unsigned)samples,44100,2)==0);
    for (frame=0;frame<12;frame++) {
        AACENC_BufDesc in={0},out={0};
        AACENC_InArgs inargs={0}; AACENC_OutArgs outargs={0};
        void *inptr=input,*outptr=encoded;
        int inid=IN_AUDIO_DATA,outid=OUT_BITSTREAM_DATA;
        int insize=samples*2*sizeof(INT_PCM),outsize=sizeof(encoded),inelement=sizeof(INT_PCM),outelement=1;
        int i, length=0, encrypted;
        uint32_t rate=0,pts=0;
        uint16_t channels=0,bits=0;
        const int16_t *pcm;
        AES_CTX aes;
        for(i=0;i<samples;i++) input[2*i]=input[2*i+1]=(INT_PCM)(12000*sin(2*3.141592653589793*440*(frame*samples+i)/44100));
        in.numBufs=out.numBufs=1;
        in.bufs=&inptr;in.bufferIdentifiers=&inid;in.bufSizes=&insize;in.bufElSizes=&inelement;
        out.bufs=&outptr;out.bufferIdentifiers=&outid;out.bufSizes=&outsize;out.bufElSizes=&outelement;
        inargs.numInSamples=samples*2;
        assert(aacEncEncode(encoder,&in,&out,&inargs,&outargs)==AACENC_OK);
        if(!outargs.numOutBytes) continue;
        memset(packet,0,12);packet[0]=0x80;packet[1]=0x60;packet[3]=(unsigned char)frame;
        encrypted=outargs.numOutBytes/16*16;
        AES_set_key(&aes,derived,iv,AES_MODE_128);
        AES_cbc_encrypt(&aes,encoded,packet+12,encrypted);
        memcpy(packet+12+encrypted,encoded+encrypted,outargs.numOutBytes-encrypted);
        assert(raop_buffer_queue(buffer,packet,(unsigned short)(outargs.numOutBytes+12),&cb)>=0);
        pcm=raop_buffer_dequeue(buffer,&length,&pts,1,&rate,&channels,&bits);
        if(!pcm) continue;
        assert(rate==44100 && channels==2 && bits==16);
        assert(length==samples*4);
        for(i=0;i<length/2;i++) energy+=(long long)pcm[i]*pcm[i];
        decoded_frames++;
    }
    assert(decoded_frames>=8 && energy>1000000);
    /* Invalid AAC must never become a successfully decoded PCM frame. */
    raop_buffer_flush(buffer,-1);
    memset(packet,0,sizeof(packet)); packet[0]=0x80;packet[1]=0x60;packet[3]=20;
    assert(raop_buffer_queue(buffer,packet,12,&cb)>=0);
    {
        int length=0;uint32_t rate=0,pts=0;uint16_t channels=0,bits=0;
        assert(!raop_buffer_dequeue(buffer,&length,&pts,1,&rate,&channels,&bits));
    }
    raop_buffer_destroy(buffer);logger_destroy(logger);aacEncClose(&encoder);
    printf("Encrypted RTP -> AAC-ELD -> PCM: %d frames decoded\n",decoded_frames);
    return;
}
int main(void){exercise(8);exercise(4);return 0;}
