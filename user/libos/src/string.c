#include <string.h>
void *memcpy(void*d,const void*s,size_t n){unsigned char*a=d;const unsigned char*b=s;while(n--)*a++=*b++;return d;}
void *memmove(void*d,const void*s,size_t n){unsigned char*a=d;const unsigned char*b=s;if(a<b)while(n--)*a++=*b++;else {a+=n;b+=n;while(n--)*--a=*--b;}return d;}
void *memset(void*d,int c,size_t n){unsigned char*a=d;while(n--)*a++=(unsigned char)c;return d;}
int memcmp(const void*a,const void*b,size_t n){const unsigned char*x=a,*y=b;while(n--){if(*x!=*y)return *x<*y?-1:1;x++;y++;}return 0;}
void *memchr(const void*p,int c,size_t n){const unsigned char*s=p;while(n--){if(*s==(unsigned char)c)return (void*)s;s++;}return 0;}
size_t strlen(const char*s){size_t n=0;while(s[n])n++;return n;}
int strcmp(const char*a,const char*b){while(*a&&*a==*b){a++;b++;}return (unsigned char)*a<(unsigned char)*b?-1:(unsigned char)*a!=(unsigned char)*b;}
int strncmp(const char*a,const char*b,size_t n){while(n&&*a&&*a==*b){a++;b++;n--;}return n?((unsigned char)*a<(unsigned char)*b?-1:(unsigned char)*a!=(unsigned char)*b):0;}
char *strcpy(char*d,const char*s){char*r=d;while((*d++=*s++));return r;}
char *strncpy(char*d,const char*s,size_t n){char*r=d;size_t i=0;while(i<n&&s[i]){d[i]=s[i];i++;}while(i<n)d[i++]=0;return r;}
char *strchr(const char*s,int c){for(;;s++){if(*s==(char)c)return (char*)s;if(!*s)return 0;}}
char *strrchr(const char*s,int c){const char*r=0;do{if(*s==(char)c)r=s;}while(*s++);return (char*)r;}
