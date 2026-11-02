#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#define LIBOS_INT_MAX 2147483647
#define LIBOS_INT_MIN (-2147483647 - 1)
#include <string.h>
#include <unistd.h>
struct libos_FILE { int fd; int owned; int has_pushback; int eof; int error; unsigned char pushback; };
static struct libos_FILE in_file={0,0,0,0,0,0},out_file={1,0,0,0,0,0},err_file={2,0,0,0,0,0};
FILE *stdin=&in_file,*stdout=&out_file,*stderr=&err_file;
static int parse_mode(const char*m,int create){if(!m)return -1;char op=m[0];if((m[1]=='\0')||((m[1]=='b'||m[1]=='t')&&m[2]=='\0')){if(op=='r')return O_RDONLY;if(op=='w')return O_WRONLY|(create?(O_CREAT|O_TRUNC):0);}return -1;}
FILE *fopen(const char*p,const char*m){int flags=parse_mode(m,1);if(flags<0){errno=EINVAL;return 0;}int fd=open(p,flags,0666);if(fd<0)return 0;FILE*f=malloc(sizeof(*f));if(!f){close(fd);return 0;}f->fd=fd;f->owned=1;f->has_pushback=f->eof=f->error=0;return f;}
FILE *fdopen(int fd,const char*m){int flags=parse_mode(m,0);if(flags<0){errno=EINVAL;return 0;}int current=fcntl(fd,F_GETFL);if(current<0)return 0;if((flags==O_RDONLY&&(current&O_ACCMODE)==O_WRONLY)||(flags==O_WRONLY&&(current&O_ACCMODE)==O_RDONLY)){errno=EBADF;return 0;}FILE*f=malloc(sizeof(*f));if(!f)return 0;f->fd=fd;f->owned=1;f->has_pushback=f->eof=f->error=0;return f;}
int fileno(FILE*f){if(!f){errno=EINVAL;return -1;}return f->fd;}
int fseek(FILE*f,long offset,int whence){if(!f){errno=EINVAL;return -1;}if(lseek(f->fd,offset,whence)<0)return -1;f->has_pushback=0;return 0;}
long ftell(FILE*f){if(!f){errno=EINVAL;return -1;}return (long)lseek(f->fd,0,SEEK_CUR);}
void rewind(FILE*f){if(f)(void)fseek(f,0,SEEK_SET);}
int fclose(FILE*f){int r;if(!f){errno=EINVAL;return -1;}r=close(f->fd);if(f->owned)free(f);return r;}
int getc(FILE*f){unsigned char c;ssize_t r;if(!f){errno=EINVAL;return EOF;}if(f->has_pushback){f->has_pushback=0;return f->pushback;}r=read(f->fd,&c,1);if(r==1)return (int)c;if(r==0)f->eof=1;else f->error=1;return EOF;}
int fgetc(FILE*f){return getc(f);}
int fputc(int byte,FILE*f){unsigned char value=(unsigned char)byte;if(!f){errno=EINVAL;return EOF;}return write(f->fd,&value,1)==1?value:EOF;}
int putc(int byte,FILE*f){return fputc(byte,f);}
int ferror(FILE*f){return f==0?1:f->error;}
int feof(FILE*f){return f==0?0:f->eof;}
void clearerr(FILE*f){if(f){f->eof=0;f->error=0;}}
FILE *popen(const char*command,const char*mode){(void)command;(void)mode;errno=ENOSYS;return NULL;}
int pclose(FILE*f){(void)f;errno=ENOSYS;return -1;}
char *mktemp(char*template){(void)template;errno=ENOSYS;return NULL;}
int ungetc(int c,FILE*f){if(c==EOF||!f||f->has_pushback)return EOF;f->pushback=(unsigned char)c;f->has_pushback=1;return f->pushback;}
char *fgets(char*s,int n,FILE*f){if(!s||!f||n<=0){errno=EINVAL;return 0;}if(n==1){s[0]=0;return s;}int used=0;while(used<n-1){int c=getc(f);if(c==EOF)break;s[used++]=(char)c;if(c=='\n')break;}if(!used)return 0;s[used]=0;return s;}
size_t fread(void*p,size_t z,size_t n,FILE*f){if(!f||(z&&n>(size_t)-1/z))return 0;size_t want=z*n,done=0;if(done<want&&f->has_pushback){((unsigned char*)p)[done++]=(unsigned char)getc(f);}while(done<want){ssize_t r=read(f->fd,(char*)p+done,want-done);if(r<=0)break;done+=(size_t)r;}return z?done/z:0;}
static int scan_space(int c){return c==' '||(c>='\t'&&c<='\r');}
static int scan_number(const char**p,int limit){int n=0;while(**p>='0'&&**p<='9'){int d=*(*p)++-'0';if(n>(limit-d)/10)return limit;n=n*10+d;}return n;}
int fscanf(FILE*f,const char*fmt,...){va_list ap;int assigned=0;if(!f||!fmt){errno=EINVAL;return EOF;}va_start(ap,fmt);while(*fmt){if(scan_space((unsigned char)*fmt)){while(scan_space((unsigned char)*fmt))fmt++;int c;do{c=getc(f);}while(c!=EOF&&scan_space(c));if(c!=EOF)ungetc(c,f);continue;}if(*fmt!='%'){int c=getc(f);if(c==EOF||c!=(unsigned char)*fmt++){if(c!=EOF)ungetc(c,f);break;}continue;}fmt++;if(*fmt=='%'){int c=getc(f);fmt++;if(c!='%'){if(c!=EOF)ungetc(c,f);break;}continue;}int width=LIBOS_INT_MAX;if(*fmt>='0'&&*fmt<='9'){width=scan_number(&fmt,LIBOS_INT_MAX);if(width==0)break;}if(*fmt=='d'){char number[32];size_t n=0;int c;char*end;long value;char*dst=va_arg(ap,char*);fmt++;c=getc(f);if(c=='+'||c=='-'){number[n++]=(char)c;c=getc(f);}while(c>='0'&&c<='9'&&n<sizeof(number)-1&&n<(size_t)width){number[n++]=(char)c;c=getc(f);}if(c!=EOF)ungetc(c,f);if(n==0||(n==1&&(number[0]=='+'||number[0]=='-'))){if(assigned==0)assigned=EOF;break;}number[n]=0;value=strtol(number,&end,10);if(*end)break;* (int*)dst=(int)value;assigned++;continue;}if(*fmt=='s'||*fmt=='['){int scanset=*fmt=='[';int negate=0,matched=0,c;size_t n=0;char*dst=va_arg(ap,char*);char terminator=0;fmt++;if(scanset){if(*fmt=='^'){negate=1;fmt++;}if(*fmt==']'){terminator=']';fmt++;}while(*fmt&&*fmt!=']'){if(terminator==0)terminator=*fmt;fmt++;}if(*fmt==']')fmt++;if(terminator==0)break;}while(n<(size_t)width){c=getc(f);if(c==EOF)break;if(scanset){matched=(c==(unsigned char)terminator);if(negate)matched=!matched;}else matched=!scan_space(c);if(!matched){ungetc(c,f);break;}dst[n++]=(char)c;}if(n==0){if(assigned==0&&(c==EOF))assigned=EOF;break;}dst[n]=0;assigned++;continue;}break;}va_end(ap);return assigned;}

static int
string_scan_match(unsigned char byte, const char *set, int negate)
{
	int found = 0;
	while (*set) {
		if (set[1] == '-' && set[2] && byte >= (unsigned char)set[0] &&
		    byte <= (unsigned char)set[2]) {
			found = 1;
			break;
		}
		if (byte == (unsigned char)*set) {
			found = 1;
			break;
		}
		set++;
	}
	return negate ? !found : found;
}

int
vsscanf(const char *input, const char *format, va_list arguments)
{
	const char *cursor = input;
	int assigned = 0;
	if (!input || !format) {
		errno = EINVAL;
		return EOF;
	}
	while (*format) {
		if (scan_space((unsigned char)*format)) {
			while (scan_space((unsigned char)*format)) format++;
			while (scan_space((unsigned char)*cursor)) cursor++;
			continue;
		}
		if (*format != '%') {
			if (*cursor != *format) break;
			cursor++;
			format++;
			continue;
		}
		format++;
		if (*format == '%') {
			if (*cursor != '%') break;
			cursor++;
			format++;
			continue;
		}
		int suppress = 0;
		int width = LIBOS_INT_MAX;
		int length = 0;
		if (*format == '*') { suppress = 1; format++; }
		if (*format >= '0' && *format <= '9') {
			width = scan_number(&format, LIBOS_INT_MAX);
			if (width == 0) break;
		}
		if (*format == 'h') { length = 1; format++; if (*format == 'h') { length = 2; format++; } }
		else if (*format == 'l') { length = 3; format++; if (*format == 'l') { length = 4; format++; } }
		else if (*format == 'z') { length = 5; format++; }
		char conversion = *format++;
		if (conversion != 'c' && conversion != '[') {
			while (scan_space((unsigned char)*cursor)) cursor++;
		}
		if (conversion == 's' || conversion == 'c' || conversion == '[') {
			char *destination = suppress ? NULL : va_arg(arguments, char *);
			int negate = 0, matched = 1;
			const char *set = format;
			if (conversion == '[') {
				if (*format == '^') { negate = 1; set = ++format; }
				if (*format == ']') format++;
				while (*format && *format != ']') format++;
				if (!*format) break;
				format++;
			}
			int limit = conversion == 'c' && width == LIBOS_INT_MAX ? 1 : width;
			int count = 0;
			while (*cursor && count < limit) {
				if (conversion == 's' && scan_space((unsigned char)*cursor)) break;
				if (conversion == '[' && !string_scan_match((unsigned char)*cursor, set, negate)) break;
				if (destination) destination[count] = *cursor;
				cursor++; count++;
			}
			if (conversion == 'c' && count != limit) matched = 0;
			else if (count == 0) matched = 0;
			if (!matched) return assigned ? assigned : EOF;
			if (destination && conversion != 'c') destination[count] = '\0';
			if (!suppress) assigned++;
			continue;
		}
		if (conversion == 'd' || conversion == 'i' || conversion == 'u' ||
		    conversion == 'o' || conversion == 'x' || conversion == 'X') {
			char number[64]; size_t count = 0; char *end;
			while (*cursor && count + 1 < sizeof(number) && count < (size_t)width) {
				unsigned char byte = (unsigned char)*cursor;
				int digit = (byte >= '0' && byte <= '9') ||
				    ((conversion == 'x' || conversion == 'X' || conversion == 'i') &&
				    ((byte >= 'a' && byte <= 'f') || (byte >= 'A' && byte <= 'F')));
				if (count == 0 && (byte == '+' || byte == '-')) digit = 1;
				if (count == 1 && number[0] == '0' &&
				    (byte == 'x' || byte == 'X') &&
				    (conversion == 'x' || conversion == 'X' || conversion == 'i')) digit = 1;
				if (!digit) break;
				number[count++] = *cursor++;
			}
			if (count == 0) return assigned ? assigned : EOF;
			number[count] = '\0';
			int base = conversion == 'o' ? 8 :
			    (conversion == 'x' || conversion == 'X' ? 16 : 10);
			long value = strtol(number, &end, base);
			if (end == number) return assigned;
			if (!suppress) {
				if (length == 3) *va_arg(arguments, long *) = value;
				else if (length == 4) *va_arg(arguments, long long *) = value;
				else if (length == 5) *va_arg(arguments, size_t *) = (size_t)value;
				else if (length == 1) *va_arg(arguments, short *) = (short)value;
				else if (length == 2) *va_arg(arguments, signed char *) = (signed char)value;
				else *va_arg(arguments, int *) = (int)value;
				assigned++;
			}
			continue;
		}
		if (conversion == 'f' || conversion == 'F' || conversion == 'g' || conversion == 'G' || conversion == 'e' || conversion == 'E') {
			char number[64]; size_t count = 0; char *end;
			while (*cursor && count + 1 < sizeof(number) && count < (size_t)width) {
				char byte = *cursor;
				if (!((byte >= '0' && byte <= '9') || byte == '+' || byte == '-' ||
				    byte == '.' || byte == 'e' || byte == 'E')) break;
				number[count++] = *cursor++;
			}
			if (count == 0) return assigned ? assigned : EOF;
			number[count] = '\0'; double value = strtod(number, &end);
			if (end == number) return assigned;
			if (!suppress) {
				if (length == 3) *va_arg(arguments, double *) = value;
				else *va_arg(arguments, float *) = (float)value;
				assigned++;
			}
			continue;
		}
		break;
	}
	return assigned;
}

int
sscanf(const char *input, const char *format, ...)
{
	va_list arguments;
	int result;
	va_start(arguments, format);
	result = vsscanf(input, format, arguments);
	va_end(arguments);
	return result;
}
size_t fwrite(const void*p,size_t z,size_t n,FILE*f){if(!f||(z&&n>(size_t)-1/z))return 0;size_t want=z*n,done=0;while(done<want){ssize_t r=write(f->fd,(const char*)p+done,want-done);if(r<=0)break;done+=(size_t)r;}return z?done/z:0;}
int fputs(const char*s,FILE*f){size_t n=strlen(s);return fwrite(s,1,n,f)==n?0:-1;}
int puts(const char*s){return fputs(s,stdout)<0||fputs("\n",stdout)<0?-1:0;}
void perror(const char*s){if(s&&*s)fprintf(stderr,"%s: %s\n",s,strerror(errno));else fprintf(stderr,"%s\n",strerror(errno));}
int fflush(FILE*f){(void)f;return 0;}
void setlinebuf(FILE*f){(void)f;}
struct out {char*p;size_t cap,n;int failed;};
static void emit(struct out*o,char c){if(o->p&&o->cap&&o->n<o->cap-1)o->p[o->n]=c;o->n++;}
static void emit_padding(struct out*o,char c,int count){while(count-->0)emit(o,c);}
static void emit_string(struct out*o,const char*s,int length,int width,int left){int padding=width>length?width-length:0;if(!left)emit_padding(o,' ',padding);for(int i=0;i<length;i++)emit(o,s[i]);if(left)emit_padding(o,' ',padding);}
static void emit_number(struct out*o,unsigned long long value,unsigned base,int negative,int upper,int width,int precision,int left,int zero,int plus,int space,int alternate,int pointer){char reversed[32];char prefix[2];unsigned digits=0,prefix_length=0;int sign=negative?'-':plus?'+':space?' ':0;const char*alphabet=upper?"0123456789ABCDEF":"0123456789abcdef";if(value!=0||precision!=0||pointer){do{reversed[digits++]=alphabet[value%base];value/=base;}while(value!=0);}if(pointer||(alternate&&base==16&&digits!=0)){prefix[prefix_length++]='0';prefix[prefix_length++]=upper?'X':'x';}if(alternate&&base==8){if(digits==0)reversed[digits++]='0';else if(reversed[digits-1]!='0')prefix[prefix_length++]='0';}int precision_zeroes=precision>(int)digits?precision-(int)digits:0;int field=(sign!=0)+(int)prefix_length+precision_zeroes+(int)digits;int padding=width>field?width-field:0;if(!left&&!zero)emit_padding(o,' ',padding);if(sign)emit(o,(char)sign);for(unsigned i=0;i<prefix_length;i++)emit(o,prefix[i]);if(!left&&zero&&precision<0)emit_padding(o,'0',padding);emit_padding(o,'0',precision_zeroes);while(digits)emit(o,reversed[--digits]);if(left)emit_padding(o,' ',padding);}
static int parse_number(const char**format,int maximum){int value=0;while(**format>='0'&&**format<='9'){int digit=*(*format)++-'0';if(value>(maximum-digit)/10)return -1;value=value*10+digit;}return value;}
static void format(struct out*o,const char*fmt,va_list ap){if(!fmt){o->failed=1;return;}while(*fmt){if(*fmt!='%'){emit(o,*fmt++);continue;}fmt++;if(*fmt=='%'){emit(o,'%');fmt++;continue;}int left=0,zero=0,plus=0,space=0,alternate=0,width=0,precision=-1,length=0;for(;;){if(*fmt=='-')left=1;else if(*fmt=='0')zero=1;else if(*fmt=='+')plus=1;else if(*fmt==' ')space=1;else if(*fmt=='#')alternate=1;else break;fmt++;}if(*fmt=='*'){width=va_arg(ap,int);fmt++;if(width<0){left=1;width=width==LIBOS_INT_MIN?LIBOS_INT_MAX:-width;}}else{width=parse_number(&fmt,LIBOS_INT_MAX);if(width<0){o->failed=1;return;}}if(*fmt=='.'){fmt++;if(*fmt=='*'){precision=va_arg(ap,int);fmt++;if(precision<0)precision=-1;}else{precision=parse_number(&fmt,LIBOS_INT_MAX);if(precision<0){o->failed=1;return;}}}if(*fmt=='h'){length=1;fmt++;if(*fmt=='h'){length=2;fmt++;}}else if(*fmt=='l'){length=3;fmt++;if(*fmt=='l'){length=4;fmt++;}}else if(*fmt=='z'||*fmt=='t'||*fmt=='j'){length=3;fmt++;}char conversion=*fmt?*fmt++:0;switch(conversion){case 's':{const char*s=va_arg(ap,const char*);int n=0;if(!s)s="(null)";while((precision<0||n<precision)&&s[n])n++;emit_string(o,s,n,width,left);break;}case 'c':{char c=(char)va_arg(ap,int);emit_string(o,&c,1,width,left);break;}case 'd':case 'i':{long long v=length>=4?va_arg(ap,long long):length==3?va_arg(ap,long):va_arg(ap,int);if(length==1)v=(short)v;else if(length==2)v=(signed char)v;unsigned long long magnitude=v<0?0ULL-(unsigned long long)v:(unsigned long long)v;emit_number(o,magnitude,10,v<0,0,width,precision,left,zero,plus,space,alternate,0);break;}case 'u':case 'o':case 'x':case 'X':case 'p':{unsigned long long v;if(conversion=='p')v=(unsigned long long)(uintptr_t)va_arg(ap,void*);else if(length>=4)v=va_arg(ap,unsigned long long);else if(length==3)v=(unsigned long long)va_arg(ap,size_t);else if(length==1)v=(unsigned short)va_arg(ap,int);else if(length==2)v=(unsigned char)va_arg(ap,int);else v=va_arg(ap,unsigned);unsigned base=conversion=='o'?8:conversion=='u'?10:16;emit_number(o,v,base,0,conversion=='X',width,precision,left,zero,0,0,alternate,conversion=='p');break;}default:o->failed=1;return;}}}
int vsnprintf(char*b,size_t n,const char*f,va_list a){struct out o={b,n,0,0};va_list copy;__builtin_va_copy(copy,a);format(&o,f,copy);__builtin_va_end(copy);if(b&&n)b[o.n<n?o.n:n-1]=0;if(o.failed||o.n>2147483647U)return -1;return (int)o.n;}
int vsprintf(char*b,const char*f,va_list a){return vsnprintf(b,(size_t)-1,f,a);}
int snprintf(char*b,size_t n,const char*f,...){va_list a;va_start(a,f);int r=vsnprintf(b,n,f,a);va_end(a);return r;}
int asprintf(char**out,const char*f,...){va_list a,copy;int n;char*b;if(!out||!f){errno=EINVAL;return -1;}*out=0;va_start(a,f);__builtin_va_copy(copy,a);n=vsnprintf(0,0,f,copy);__builtin_va_end(copy);if(n<0){va_end(a);return -1;}b=malloc((size_t)n+1);if(!b){va_end(a);return -1;}__builtin_va_copy(copy,a);if(vsnprintf(b,(size_t)n+1,f,copy)<0){__builtin_va_end(copy);free(b);va_end(a);return -1;}__builtin_va_end(copy);va_end(a);*out=b;return n;}
int sprintf(char*b,const char*f,...){va_list a;va_start(a,f);int r=vsprintf(b,f,a);va_end(a);return r;}
int vfprintf(FILE*f,const char*fmt,va_list a){int n;char*b;size_t done=0;va_list copy;if(!f||!fmt){errno=EINVAL;return -1;}__builtin_va_copy(copy,a);n=vsnprintf(0,0,fmt,copy);__builtin_va_end(copy);if(n<0)return -1;b=malloc((size_t)n+1);if(!b)return -1;__builtin_va_copy(copy,a);if(vsnprintf(b,(size_t)n+1,fmt,copy)<0){__builtin_va_end(copy);free(b);return -1;}__builtin_va_end(copy);while(done<(size_t)n){size_t w=fwrite(b+done,1,(size_t)n-done,f);if(w==0){free(b);return -1;}done+=w;}free(b);return n;}
int fprintf(FILE*f,const char*fmt,...){va_list a;va_start(a,fmt);int r=vfprintf(f,fmt,a);va_end(a);return r;}
int printf(const char*f,...){va_list a,copy;int n;char*b;size_t done=0;va_start(a,f);__builtin_va_copy(copy,a);n=vsnprintf(0,0,f,copy);__builtin_va_end(copy);if(n<0){va_end(a);return -1;}b=malloc((size_t)n+1);if(!b){va_end(a);return -1;}__builtin_va_copy(copy,a);(void)vsnprintf(b,(size_t)n+1,f,copy);__builtin_va_end(copy);va_end(a);while(done<(size_t)n){size_t w=fwrite(b+done,1,(size_t)n-done,stdout);if(w==0){free(b);return -1;}done+=w;}free(b);return n;}
int vprintf(const char*f,va_list a){return vfprintf(stdout,f,a);}
int putchar(int c){return fputc(c,stdout);}
