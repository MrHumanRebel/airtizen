#include "airtizen_metadata.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void){
    unsigned char tags[]={'m','l','i','t',0,0,0,31,'m','i','n','m',0,0,0,7,'<','t','i','t','l','e','>','a','s','a','r',0,0,0,8,'A','r','t','i','s','t',' ','A'};
    unsigned char png[]={0x89,'P','N','G',13,10,26,10,0,0,0,0};
    airtizen_metadata_state state;unsigned char *copy=NULL;char mime[16];
    assert(airtizen_metadata_parse(tags,sizeof(tags))==0);airtizen_metadata_snapshot(&state);
    assert(!strcmp(state.title,"<title>") && !strcmp(state.artist,"Artist A"));
    assert(airtizen_metadata_parse(tags,sizeof(tags)-1)<0);airtizen_metadata_snapshot(&state);assert(!strcmp(state.title,"<title>"));
    assert(airtizen_metadata_artwork("<svg>bad</svg>",14)<0);
    assert(airtizen_metadata_artwork(png,sizeof(png))==0);airtizen_metadata_snapshot(&state);assert(state.artwork);
    assert(airtizen_metadata_copy_artwork(state.artwork,&copy,mime)==sizeof(png));assert(!strcmp(mime,"image/png") && !memcmp(copy,png,sizeof(png)));free(copy);
    airtizen_metadata_progress(0xfffffff0U,16,48);airtizen_metadata_snapshot(&state);assert(state.progress==32 && state.total==64);
    airtizen_metadata_progress(0,100,50);airtizen_metadata_snapshot(&state);assert(state.progress==32);
    airtizen_metadata_reset();assert(!airtizen_metadata_copy_artwork(state.artwork,&copy,mime));
    airtizen_metadata_snapshot(&state);assert(!state.artwork && !state.title[0]);
    puts("DMAP text, truncated input, artwork validation, stale image rejection and RTP progress wrap passed");return 0;
}
