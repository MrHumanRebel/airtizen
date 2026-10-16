#include "airtizen_alac.h"
#include "raop_buffer.h"
#include "crypto/crypto.h"
#include "ed25519/sha512.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static size_t read_fixture(const char *name,unsigned char *out,size_t capacity) {
    char path[1024];FILE *file;size_t size;
    snprintf(path,sizeof(path),"%s/%s",AIRTIZEN_FIXTURE_DIR,name);
    file=fopen(path,"rb");assert(file);size=fread(out,1,capacity,file);assert(feof(file));fclose(file);return size;
}
int main(void) {
    unsigned char encoded[4096],expected[4096],packet[4108],key[16]={0},iv[16]={0},secret[32]={0},derived[64];
    int16_t pcm[4096];
    size_t size=read_fixture("stereo-44100-352.alac",encoded,sizeof(encoded));
    size_t expected_size=read_fixture("stereo-44100-352.pcm",expected,sizeof(expected));
    airtizen_alac *decoder=airtizen_alac_create(44100,2,352);
    logger_t *logger=logger_init();raop_buffer_t *buffer;
    sha512_context hash;AES_CTX aes;
    int length=0;unsigned pts=0;uint32_t rate=0;uint16_t channels=0,bits=0;
    const void *decoded;
    assert(decoder && size>0 && expected_size==352*2*2);
    assert(airtizen_alac_decode(decoder,encoded,size,pcm,sizeof(pcm))==expected_size);
    assert(memcmp(pcm,expected,expected_size)==0);
    assert(airtizen_alac_decode(decoder,encoded,size,pcm,4)==0);
    assert(!airtizen_alac_create(96000,2,352));
    assert(!airtizen_alac_create(44100,8,352));
    assert(!airtizen_alac_create(44100,2,65535));
    assert(airtizen_alac_decode(decoder,NULL,0,pcm,sizeof(pcm))==0);
    airtizen_alac_flush(decoder);airtizen_alac_destroy(decoder);
    sha512_init(&hash);sha512_update(&hash,key,16);sha512_update(&hash,secret,32);sha512_final(&hash,derived);
    buffer=raop_buffer_init(logger,key,iv,secret);assert(buffer);
    assert(raop_buffer_configure_audio(buffer,2,352,44100,2)==0);
    memset(packet,0,12);packet[0]=0x80;packet[1]=0x60;packet[3]=1;packet[7]=42;
    AES_set_key(&aes,derived,iv,AES_MODE_128);
    AES_cbc_encrypt(&aes,encoded,packet+12,(int)(size/16*16));
    memcpy(packet+12+size/16*16,encoded+size/16*16,size%16);
    assert(raop_buffer_queue(buffer,packet,(unsigned short)(size+12),NULL)==1);
    decoded=raop_buffer_dequeue(buffer,&length,&pts,1,&rate,&channels,&bits);
    assert(decoded && length==(int)expected_size && pts==42 && rate==44100 && channels==2 && bits==16);
    assert(memcmp(decoded,expected,expected_size)==0);
    assert(raop_buffer_configure_audio(buffer,999,352,44100,2)<0);
    assert(raop_buffer_configure_audio(buffer,8,480,44100,2)==0);
    raop_buffer_destroy(buffer);logger_destroy(logger);
    puts("Lossless ALAC and encrypted RTP -> ALAC -> stereo PCM match the independent fixture exactly");
    return 0;
}
