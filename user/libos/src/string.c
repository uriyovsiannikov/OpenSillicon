#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
void *memcpy(void*d,const void*s,size_t n){unsigned char*a=d;const unsigned char*b=s;while(n--)*a++=*b++;return d;}
void *memmove(void*d,const void*s,size_t n){unsigned char*a=d;const unsigned char*b=s;if(a<b)while(n--)*a++=*b++;else {a+=n;b+=n;while(n--)*--a=*--b;}return d;}
void *memset(void*d,int c,size_t n){unsigned char*a=d;while(n--)*a++=(unsigned char)c;return d;}
int memcmp(const void*a,const void*b,size_t n){const unsigned char*x=a,*y=b;while(n--){if(*x!=*y)return *x<*y?-1:1;x++;y++;}return 0;}
void *memchr(const void*p,int c,size_t n){const unsigned char*s=p;while(n--){if(*s==(unsigned char)c)return (void*)s;s++;}return 0;}
size_t strlen(const char*s){size_t n=0;while(s[n])n++;return n;}
size_t strnlen(const char*s,size_t n){size_t i=0;while(i<n&&s[i])i++;return i;}
int strcmp(const char*a,const char*b){while(*a&&*a==*b){a++;b++;}return (unsigned char)*a<(unsigned char)*b?-1:(unsigned char)*a!=(unsigned char)*b;}
int strncmp(const char*a,const char*b,size_t n){while(n&&*a&&*a==*b){a++;b++;n--;}return n?((unsigned char)*a<(unsigned char)*b?-1:(unsigned char)*a!=(unsigned char)*b):0;}
int strcoll(const char*a,const char*b){return strcmp(a,b);}
char *strcpy(char*d,const char*s){char*r=d;while((*d++=*s++));return r;}
char *stpcpy(char*d,const char*s){while((*d=*s)){d++;s++;}return d;}
char *strcat(char*d,const char*s){stpcpy(d+strlen(d),s);return d;}
char *strncat(char*d,const char*s,size_t n){size_t len=strlen(d),i=0;while(i<n&&s[i]){d[len+i]=s[i];i++;}d[len+i]=0;return d;}
size_t strlcat(char*d,const char*s,size_t n){size_t dlen=strnlen(d,n),slen=strlen(s),i;if(dlen<n){for(i=0;i<slen&&dlen+i+1<n;i++)d[dlen+i]=s[i];d[dlen+i]=0;}return dlen+slen;}
char *strncpy(char*d,const char*s,size_t n){char*r=d;size_t i=0;while(i<n&&s[i]){d[i]=s[i];i++;}while(i<n)d[i++]=0;return r;}
char *strchr(const char*s,int c){for(;;s++){if(*s==(char)c)return (char*)s;if(!*s)return 0;}}
char *strrchr(const char*s,int c){const char*r=0;do{if(*s==(char)c)r=s;}while(*s++);return (char*)r;}
char *strstr(const char*h,const char*n){size_t len;if(!*n)return (char*)h;len=strlen(n);for(;*h;h++)if(*h==*n&&strncmp(h,n,len)==0)return (char*)h;return 0;}
char *strtok(char *s,const char *delim){static char *next;char *start;if(s)next=s;else if(!next)return 0;while(*next&&strchr(delim,*next))next++;if(!*next){next=0;return 0;}start=next;while(*next&&!strchr(delim,*next))next++;if(*next)*next++=0;else next=0;return start;}
char *strdup(const char*s){size_t n;char*d;if(!s){errno=EINVAL;return 0;}n=strlen(s);if(n==(size_t)-1){errno=ENOMEM;return 0;}d=malloc(n+1);if(!d)return 0;memcpy(d,s,n+1);return d;}
char *strerror(int e){switch(e){case 0:return "Success";case EPERM:return "Operation not permitted";case ENOENT:return "No such file or directory";case ESRCH:return "No such process";case EINTR:return "Interrupted system call";case EIO:return "Input/output error";case ENXIO:return "No such device or address";case E2BIG:return "Argument list too long";case ENOEXEC:return "Exec format error";case EBADF:return "Bad file descriptor";case ECHILD:return "No child processes";case EAGAIN:return "Resource temporarily unavailable";case ENOMEM:return "Cannot allocate memory";case EACCES:return "Permission denied";case EFAULT:return "Bad address";case EBUSY:return "Device or resource busy";case EEXIST:return "File exists";case EXDEV:return "Invalid cross-device link";case ENODEV:return "No such device";case ENOTDIR:return "Not a directory";case EISDIR:return "Is a directory";case EINVAL:return "Invalid argument";case ENFILE:return "Too many open files in system";case EMFILE:return "Too many open files";case ENOTTY:return "Inappropriate ioctl for device";case EFBIG:return "File too large";case ENOSPC:return "No space left on device";case ESPIPE:return "Illegal seek";case EROFS:return "Read-only file system";case EPIPE:return "Broken pipe";case ERANGE:return "Numerical result out of range";case ENAMETOOLONG:return "File name too long";case ENOSYS:return "Function not implemented";case ENOTSOCK:return "Socket operation on non-socket";case EPROTONOSUPPORT:return "Protocol not supported";case EAFNOSUPPORT:return "Address family not supported";case EADDRINUSE:return "Address already in use";case ENOBUFS:return "No buffer space available";case EOPNOTSUPP:return "Operation not supported";case ENOTCONN:return "Transport endpoint is not connected";case ECONNREFUSED:return "Connection refused";case EOVERFLOW:return "Value too large";default:return "Unknown error";}}
static unsigned char ascii_lower(unsigned char c){return c>='A'&&c<='Z'?(unsigned char)(c+('a'-'A')):c;}
int strncasecmp(const char*a,const char*b,size_t n){while(n){unsigned char x=ascii_lower((unsigned char)*a),y=ascii_lower((unsigned char)*b);if(x!=y)return x<y?-1:1;if(!x)return 0;a++;b++;n--;}return 0;}
int strcasecmp(const char*a,const char*b){for(;;a++,b++){unsigned char x=ascii_lower((unsigned char)*a),y=ascii_lower((unsigned char)*b);if(x!=y)return x<y?-1:1;if(!x)return 0;}}
void bzero(void *pointer,size_t length){memset(pointer,0,length);}
int __popcountdi2(unsigned long long value){int count=0;while(value){value&=value-1;count++;}return count;}
size_t wcslen(const wchar_t*s){size_t n=0;while(s[n])n++;return n;}
wchar_t *wcscpy(wchar_t*d,const wchar_t*s){wchar_t*r=d;while((*d++=*s++));return r;}
wchar_t *wcsncpy(wchar_t*d,const wchar_t*s,size_t n){size_t i=0;while(i<n&&s[i]){d[i]=s[i];i++;}while(i<n)d[i++]=0;return d;}
int iswalnum(wint_t c){return (c>='0'&&c<='9')||(c>='A'&&c<='Z')||(c>='a'&&c<='z');}
