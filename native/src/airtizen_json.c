#include "airtizen_json.h"
#include <stdint.h>
#include <string.h>
int airtizen_json_quote(char *out, size_t capacity, const char *text) {
    static const char hex[] = "0123456789abcdef";
    size_t n = 0;
    const unsigned char *p = (const unsigned char *)(text ? text : "");
    if (capacity < 3) return -1;
    out[n++] = '"';
    while (*p) {
        char encoded[6];
        size_t need = 1, i;
        if (*p < 32) {
            memcpy(encoded,"\\u0000",6);encoded[4]=hex[*p >> 4];encoded[5]=hex[*p & 15];need=6;
        } else if (*p == '"' || *p == '\\') {
            encoded[0]='\\';encoded[1]=(char)*p;need=2;
        } else if (*p >= 128) {
            size_t length = *p >= 0xc2 && *p <= 0xdf ? 2 : *p >= 0xe0 && *p <= 0xef ? 3 : *p >= 0xf0 && *p <= 0xf4 ? 4 : 0;
            uint32_t codepoint = length ? *p & ((1U << (7-length))-1) : 0;
            int valid = length != 0;
            for (i=1; valid && i<length; ++i) {
                if (!p[i] || (p[i]&0xc0)!=0x80) valid=0;
                else codepoint=(codepoint<<6)|(p[i]&0x3f);
            }
            if (valid && ((length==3 && codepoint<0x800) || (length==4 && codepoint<0x10000) ||
                          codepoint>0x10ffff || (codepoint>=0xd800 && codepoint<=0xdfff))) valid=0;
            if (valid) {
                if (n+length+2>capacity) { out[0]=0;return -1; }
                memcpy(out+n,p,length);n+=length;p+=length;continue;
            }
            memcpy(encoded,"\\ufffd",6);need=6;
        } else encoded[0]=(char)*p;
        if (n+need+2>capacity) { out[0]=0;return -1; }
        memcpy(out+n,encoded,need);n+=need;++p;
    }
    out[n++]='"';out[n]=0;
    return (int)n;
}
