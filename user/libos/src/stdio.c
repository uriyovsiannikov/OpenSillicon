#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
struct libos_FILE { int fd; int owned; };
static struct libos_FILE in_file={0,0},out_file={1,0},err_file={2,0};
FILE *stdin=&in_file,*stdout=&out_file,*stderr=&err_file;
FILE *fopen(const char*p,const char*m){int flags;if(!m){errno=EINVAL;return 0;}if(m[0]=='r'&&m[1]=='\0')flags=O_RDONLY;else if(m[0]=='w'&&m[1]=='\0')flags=O_WRONLY|O_CREAT|O_TRUNC;else{errno=EINVAL;return 0;}int fd=open(p,flags,0666);if(fd<0)return 0;FILE*f=malloc(sizeof(*f));if(!f){close(fd);return 0;}f->fd=fd;f->owned=1;return f;}
int fclose(FILE*f){int r;if(!f){errno=EINVAL;return -1;}r=close(f->fd);if(f->owned)free(f);return r;}
size_t fread(void*p,size_t z,size_t n,FILE*f){if(!f||(z&&n>(size_t)-1/z))return 0;size_t want=z*n,done=0;while(done<want){ssize_t r=read(f->fd,(char*)p+done,want-done);if(r<=0)break;done+=(size_t)r;}return z?done/z:0;}
size_t fwrite(const void*p,size_t z,size_t n,FILE*f){if(!f||(z&&n>(size_t)-1/z))return 0;size_t want=z*n,done=0;while(done<want){ssize_t r=write(f->fd,(const char*)p+done,want-done);if(r<=0)break;done+=(size_t)r;}return z?done/z:0;}
int fputs(const char*s,FILE*f){size_t n=strlen(s);return fwrite(s,1,n,f)==n?0:-1;}
int puts(const char*s){return fputs(s,stdout)<0||fputs("\n",stdout)<0?-1:0;}
struct out {char*p;size_t cap,n;int failed;};
static void emit(struct out*o,char c){if(o->p&&o->cap&&o->n<o->cap-1)o->p[o->n]=c;o->n++;}
static void number(struct out*o,unsigned long long v,unsigned base,int neg,int upper){char b[32];unsigned n=0;const char*digits=upper?"0123456789ABCDEF":"0123456789abcdef";if(neg)emit(o,'-');do{b[n++]=digits[v%base];v/=base;}while(v);while(n)emit(o,b[--n]);}
static void format(struct out*o,const char*fmt,va_list ap){while(*fmt){if(*fmt!='%'){emit(o,*fmt++);continue;}fmt++;if(*fmt=='%'){emit(o,'%');fmt++;continue;}int longarg=0;if(*fmt=='l'){longarg=1;fmt++;if(*fmt=='l'){longarg=2;fmt++;}}char c=*fmt?*fmt++:0;switch(c){case 's':{const char*s=va_arg(ap,const char*);if(!s)s="(null)";while(*s)emit(o,*s++);break;}case 'c':emit(o,(char)va_arg(ap,int));break;case 'd':case 'i':{long long v=longarg==2?va_arg(ap,long long):longarg?va_arg(ap,long):va_arg(ap,int);unsigned long long u=v<0?0ULL-(unsigned long long)v:(unsigned long long)v;number(o,u,10,v<0,0);break;}case 'u':case 'x':case 'X':case 'p':{unsigned long long v=c=='p'?(unsigned long long)(uintptr_t)va_arg(ap,void*):longarg==2?va_arg(ap,unsigned long long):longarg?va_arg(ap,unsigned long):va_arg(ap,unsigned);if(c=='p'){emit(o,'0');emit(o,'x');}number(o,v,(c=='x'||c=='X'||c=='p')?16:10,0,c=='X');break;}default:o->failed=1;return;}}}
int vsnprintf(char*b,size_t n,const char*f,va_list a){struct out o={b,n,0,0};va_list copy;__builtin_va_copy(copy,a);format(&o,f,copy);__builtin_va_end(copy);if(b&&n)b[o.n<n?o.n:n-1]=0;if(o.failed||o.n>2147483647U)return -1;return (int)o.n;}
int snprintf(char*b,size_t n,const char*f,...){va_list a;va_start(a,f);int r=vsnprintf(b,n,f,a);va_end(a);return r;}
int printf(const char*f,...){va_list a,copy;int n;char*b;size_t done=0;va_start(a,f);__builtin_va_copy(copy,a);n=vsnprintf(0,0,f,copy);__builtin_va_end(copy);if(n<0){va_end(a);return -1;}b=malloc((size_t)n+1);if(!b){va_end(a);return -1;}__builtin_va_copy(copy,a);(void)vsnprintf(b,(size_t)n+1,f,copy);__builtin_va_end(copy);va_end(a);while(done<(size_t)n){size_t w=fwrite(b+done,1,(size_t)n-done,stdout);if(w==0){free(b);return -1;}done+=w;}free(b);return n;}
