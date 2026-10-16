#include "airtizen_h264.h"
#include <string.h>
size_t airtizen_h264_config(const uint8_t *data,size_t size,uint8_t *out,size_t capacity) {
    size_t position=6,used=0;unsigned group,count,i;
    if (!data || !out || size<7 || data[0]!=1 || (data[4]&3)!=3) return 0;
    count=data[5]&31;if(!count)return 0;
    for(group=0;group<2;++group) {
        for(i=0;i<count;++i) {
            size_t length;
            if(position+2>size)return 0;
            length=((size_t)data[position]<<8)|data[position+1];position+=2;
            if(!length || length>size-position || used+4+length>capacity || (data[position]&31)!=(group?8:7))return 0;
            memcpy(out+used,"\0\0\0\1",4);used+=4;memcpy(out+used,data+position,length);used+=length;position+=length;
        }
        if(!group) { if(position>=size)return 0;count=data[position++];if(!count)return 0; }
    }
    return used;
}
int airtizen_h264_frame(uint8_t *data,size_t size) {
    size_t position=0;
    if(!data || size<5)return -1;
    /* Validate every length before modifying the access unit. */
    while(position<size) {
        uint32_t length;
        if(size-position<4)return -1;
        length=((uint32_t)data[position]<<24)|((uint32_t)data[position+1]<<16)|((uint32_t)data[position+2]<<8)|data[position+3];
        if(!length || length>size-position-4)return -1;
        position+=4+length;
    }
    position=0;
    while(position<size) {
        uint32_t length=((uint32_t)data[position]<<24)|((uint32_t)data[position+1]<<16)|((uint32_t)data[position+2]<<8)|data[position+3];
        memcpy(data+position,"\0\0\0\1",4);position+=4+length;
    }
    return 0;
}
