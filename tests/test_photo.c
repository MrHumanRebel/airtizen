#include "airtizen_photo.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
/* Header-only payload: tests bounded header validation/cache, not image decode. */
static unsigned char jpeg[]={255,216,255,192,0,17,8,0,2,0,3,3,1,17,0,2,17,0,3,17,0,255,217};
int main(void){
    airtizen_photo_state a,b;unsigned i;uint8_t *out;char mime[16],key[16];
    airtizen_photo_reset();
    assert(airtizen_photo_put("displayCached","missing","session",NULL,NULL,0)==412);
    assert(airtizen_photo_put("cacheOnly","a","session",NULL,jpeg,sizeof(jpeg))==200);
    airtizen_photo_snapshot(&a);assert(!a.active);
    assert(airtizen_photo_put("displayCached","a","other",NULL,NULL,0)==412);
    assert(airtizen_photo_put("displayCached","a","session","Dissolve",NULL,0)==200);
    airtizen_photo_snapshot(&a);assert(a.active && a.width==3 && a.height==2 && a.dissolve && !a.ready);
    assert(airtizen_photo_report(a.generation,0)==0);
    for(i=0;i<8;++i){key[0]='b'+i;key[1]=0;assert(airtizen_photo_put("cacheOnly",key,"session",NULL,jpeg,sizeof(jpeg))==200);}
    /* The displayed image remains pinned even when preload entries are evicted. */
    assert(airtizen_photo_copy(a.generation,&out,mime)==sizeof(jpeg));assert(!memcmp(out,jpeg,sizeof(jpeg)));free(out);assert(!strcmp(mime,"image/jpeg"));
    assert(airtizen_photo_put("displayCached","b","session",NULL,NULL,0)==412);
    assert(airtizen_photo_put(NULL,"bad","session",NULL,"<svg/>",6)==400);
    assert(airtizen_photo_put("invalid","a","session",NULL,jpeg,sizeof(jpeg))==400);
    assert(airtizen_photo_put(NULL,"a","session",NULL,jpeg,AIRTIZEN_PHOTO_MAX_BYTES+1)==413);
    jpeg[7]=255;jpeg[8]=255;assert(airtizen_photo_put(NULL,NULL,NULL,NULL,jpeg,sizeof(jpeg))==400);jpeg[7]=0;jpeg[8]=2;
    airtizen_photo_snapshot(&b);assert(b.generation==a.generation && b.ready && b.session_id==a.session_id);
    {
        unsigned char *large=calloc(1,AIRTIZEN_PHOTO_MAX_BYTES);assert(large);memcpy(large,jpeg,sizeof(jpeg));
        assert(airtizen_photo_put("cacheOnly","large-a","session",NULL,large,AIRTIZEN_PHOTO_MAX_BYTES)==200);
        assert(airtizen_photo_put("cacheOnly","large-b","session",NULL,large,AIRTIZEN_PHOTO_MAX_BYTES)==200);
        assert(airtizen_photo_put("displayCached","large-a","session",NULL,NULL,0)==412);
        assert(airtizen_photo_copy(a.generation,&out,mime)==sizeof(jpeg));free(out);
        free(large);
    }
    airtizen_photo_reset();assert(!airtizen_photo_copy(a.generation,&out,mime));assert(!out);assert(airtizen_photo_report(a.generation,0)<0);
    assert(airtizen_photo_put("displayCached","a","session",NULL,NULL,0)==412);
    return 0;
}
