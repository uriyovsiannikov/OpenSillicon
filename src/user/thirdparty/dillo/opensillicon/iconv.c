/* Stateless Unicode conversion used by the native Dillo port. */
#include "iconv.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <errno.h>
#ifndef EILSEQ
#define EILSEQ 84
#endif
enum { UTF8, LATIN1, CP1251, CP1252, ASCII, UTF16LE, UTF16BE };
struct os_iconv { int from, to; };
static const unsigned short cp1251[128] = {
    0x402, 0x403, 0x201a, 0x453, 0x201e, 0x2026, 0x2020, 0x2021,
    0x20ac, 0x2030, 0x409, 0x2039, 0x40a, 0x40c, 0x40b, 0x40f,
    0x452, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
    0xfffd, 0x2122, 0x459, 0x203a, 0x45a, 0x45c, 0x45b, 0x45f,
    0xa0, 0x40e, 0x45e, 0x408, 0xa4, 0x490, 0xa6, 0xa7,
    0x401, 0xa9, 0x404, 0xab, 0xac, 0xad, 0xae, 0x407,
    0xb0, 0xb1, 0x406, 0x456, 0x491, 0xb5, 0xb6, 0xb7,
    0x451, 0x2116, 0x454, 0xbb, 0x458, 0x405, 0x455, 0x457,
    0x410, 0x411, 0x412, 0x413, 0x414, 0x415, 0x416, 0x417,
    0x418, 0x419, 0x41a, 0x41b, 0x41c, 0x41d, 0x41e, 0x41f,
    0x420, 0x421, 0x422, 0x423, 0x424, 0x425, 0x426, 0x427,
    0x428, 0x429, 0x42a, 0x42b, 0x42c, 0x42d, 0x42e, 0x42f,
    0x430, 0x431, 0x432, 0x433, 0x434, 0x435, 0x436, 0x437,
    0x438, 0x439, 0x43a, 0x43b, 0x43c, 0x43d, 0x43e, 0x43f,
    0x440, 0x441, 0x442, 0x443, 0x444, 0x445, 0x446, 0x447,
    0x448, 0x449, 0x44a, 0x44b, 0x44c, 0x44d, 0x44e, 0x44f,
};
static const unsigned short cp1252[128] = {
    0x20ac, 0xfffd, 0x201a, 0x192, 0x201e, 0x2026, 0x2020, 0x2021,
    0x2c6, 0x2030, 0x160, 0x2039, 0x152, 0xfffd, 0x17d, 0xfffd,
    0xfffd, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
    0x2dc, 0x2122, 0x161, 0x203a, 0x153, 0xfffd, 0x17e, 0x178,
    0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
    0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
    0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7,
    0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf,
    0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,
    0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf,
    0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7,
    0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf,
    0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7,
    0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef,
    0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
    0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff,
};

static int encoding(const char *s)
{
    if (!strcasecmp(s,"UTF-8") || !strcasecmp(s,"UTF8")) return UTF8;
    if (!strcasecmp(s,"ISO-8859-1") || !strcasecmp(s,"LATIN1")) return LATIN1;
    if (!strcasecmp(s,"WINDOWS-1251") || !strcasecmp(s,"CP1251")) return CP1251;
    if (!strcasecmp(s,"WINDOWS-1252") || !strcasecmp(s,"CP1252")) return CP1252;
    if (!strcasecmp(s,"ASCII") || !strcasecmp(s,"US-ASCII")) return ASCII;
    if (!strcasecmp(s,"UTF-16LE")) return UTF16LE;
    if (!strcasecmp(s,"UTF-16BE")) return UTF16BE;
    return -1;
}
iconv_t iconv_open(const char *to, const char *from)
{
    int a=encoding(from), b=encoding(to);
    if (a<0 || b<0) { errno=EINVAL; return (iconv_t)-1; }
    iconv_t c=malloc(sizeof(*c));
    if (!c) { errno=ENOMEM; return (iconv_t)-1; }
    c->from=a; c->to=b; return c;
}
int iconv_close(iconv_t c)
{
    if (!c || c==(iconv_t)-1) { errno=EBADF; return -1; }
    free(c); return 0;
}
static unsigned word(const unsigned char *s, int be)
{ return be ? ((unsigned)s[0]<<8)|s[1] : ((unsigned)s[1]<<8)|s[0]; }
size_t iconv(iconv_t c, char **in, size_t *left, char **out, size_t *room)
{
    if (!c || c==(iconv_t)-1) { errno=EBADF; return (size_t)-1; }
    if (!in || !*in) return 0;
    while (*left) {
        const unsigned char *s=(const unsigned char *)*in;
        uint32_t cp=s[0]; size_t n=1, m=0;
        unsigned char buf[4];
        if (c->from==UTF8 && cp>=128) {
            if (cp>=0xc2 && cp<=0xdf) { cp&=31; n=2; }
            else if (cp>=0xe0 && cp<=0xef) { cp&=15; n=3; }
            else if (cp>=0xf0 && cp<=0xf4) { cp&=7; n=4; }
            else goto invalid;
            if (*left<n) goto incomplete;
            for (size_t i=1;i<n;i++) {
                if ((s[i]&0xc0)!=0x80) goto invalid;
                cp=(cp<<6)|(s[i]&63);
            }
            if ((n==2 && cp<128) || (n==3 && cp<2048) ||
                (n==4 && cp<65536) || cp>0x10ffff ||
                (cp>=0xd800 && cp<=0xdfff)) goto invalid;
        } else if (c->from==UTF16LE || c->from==UTF16BE) {
            if (*left<2) goto incomplete;
            cp=word(s,c->from==UTF16BE); n=2;
            if (cp>=0xd800 && cp<=0xdbff) {
                if (*left<4) goto incomplete;
                unsigned low=word(s+2,c->from==UTF16BE);
                if (low<0xdc00 || low>0xdfff) goto invalid;
                cp=0x10000+((cp-0xd800)<<10)+(low-0xdc00); n=4;
            } else if (cp>=0xdc00 && cp<=0xdfff) goto invalid;
        } else if (c->from==ASCII && cp>=128) goto invalid;
        else if (c->from==CP1251 && cp>=128) cp=cp1251[cp-128];
        else if (c->from==CP1252 && cp>=128) cp=cp1252[cp-128];
        if (cp==0xfffd && c->from!=UTF8 &&
            c->from!=UTF16LE && c->from!=UTF16BE) goto invalid;
        if (c->to==UTF8) {
            if (cp<128) buf[m++]=cp;
            else if (cp<2048) { buf[m++]=0xc0|(cp>>6); buf[m++]=0x80|(cp&63); }
            else if (cp<65536) {
                buf[m++]=0xe0|(cp>>12); buf[m++]=0x80|((cp>>6)&63); buf[m++]=0x80|(cp&63);
            } else {
                buf[m++]=0xf0|(cp>>18); buf[m++]=0x80|((cp>>12)&63);
                buf[m++]=0x80|((cp>>6)&63); buf[m++]=0x80|(cp&63);
            }
        } else if (c->to==UTF16LE || c->to==UTF16BE) {
            unsigned units[2], count=1;
            units[0]=cp;
            if (cp>=65536) {
                cp-=65536; units[0]=0xd800+(cp>>10);
                units[1]=0xdc00+(cp&1023); count=2;
            }
            for (unsigned i=0;i<count;i++) {
                if (c->to==UTF16BE) { buf[m++]=units[i]>>8; buf[m++]=units[i]; }
                else { buf[m++]=units[i]; buf[m++]=units[i]>>8; }
            }
        } else {
            unsigned limit=c->to==ASCII ? 127 : 255;
            if (c->to==CP1251 || c->to==CP1252) {
                if (cp>=128) {
                    const unsigned short *table=c->to==CP1251?cp1251:cp1252;
                    unsigned i; for (i=0;i<128 && table[i]!=cp;i++) {}
                    if (i==128) goto invalid;
                    cp=i+128;
                }
            } else if (cp>limit) goto invalid;
            buf[m++]=cp;
        }
        if (*room<m) { errno=E2BIG; return (size_t)-1; }
        memcpy(*out,buf,m); *out+=m; *room-=m; *in+=n; *left-=n;
    }
    return 0;
invalid: errno=EILSEQ; return (size_t)-1;
incomplete: errno=EINVAL; return (size_t)-1;
}
