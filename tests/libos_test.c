#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
static int check(int ok,int code){if(!ok){printf("libos fixture failure %d\n",code);return code;}return 0;}
int main(int argc,char**argv,char**envp){char b[64];struct stat st,fdst;int fd;DIR*d;struct dirent*e;int seen=0;(void)argv;(void)envp;
 if(check(argc==0&&argv&&argv[0]==0&&envp&&envp[0]==0,1))return 1;
 if(check(strlen("libos")==5&&strcmp("abc","abc")==0&&strncmp("abc","abd",2)==0,2))return 2;
 memset(b,0,sizeof b);memcpy(b,"native",7);if(check(memcmp(b,"native",7)==0&&memchr(b,'v',7)==b+4,3))return 3;
 memmove(b+1,b,6);if(check(memcmp(b+1,"native",7)==0,4))return 4;
 if(check(strcmp(strcpy(b,"hello"),"hello")==0&&strchr(b,'e')==b+1&&strrchr(b,'l')==b+3,5))return 5;
 memset(b,0,sizeof b);strncpy(b,"xy",sizeof b);if(check(b[0]=='x'&&b[1]=='y'&&b[2]==0,6))return 6;
 void*p=calloc(4,4);if(check(p&&((unsigned long)p%16)==0,7))return 7;for(int i=0;i<16;i++)if(((char*)p)[i])return 8;memcpy(p,"xyz",4);p=realloc(p,32);if(check(p&&strcmp(p,"xyz")==0,9))return 9;free(p);
 if(check(snprintf(b,sizeof b,"%s %d %x","ok",-7,42)==8&&strcmp(b,"ok -7 2a")==0,10))return 10;
 fd=open("/etc/passwd",O_RDONLY);if(check(fd>=0,11))return 11;if(fstat(fd,&fdst)!=0||!S_ISREG(fdst.st_mode)||fdst.st_size<=0)return check(0,12);
 if(check(lseek(fd,1,SEEK_SET)==1&&fstat(fd,&st)==0&&lseek(fd,0,SEEK_CUR)==1,13))return 13;
 if(check(lseek(fd,0,SEEK_SET)==0&&read(fd,b,1)==1&&b[0]=='r',14))return 14;
 close(fd);
 {FILE*f=fopen("/etc/passwd","r");if(check(f!=0,15))return 15;if(check(fread(b,1,1,f)==1&&b[0]=='r',16))return 16;if(check(fclose(f)==0,17))return 17;}
 if(check(stat("/bin",&st)==0&&S_ISDIR(st.st_mode),18))return 18;
 if(check(fstatat(AT_FDCWD,"/etc/passwd",&st,0)==0&&S_ISREG(st.st_mode)&&st.st_size==fdst.st_size&&st.st_ino==fdst.st_ino,19))return 19;
 {void*m=mmap(0,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);if(check(m!=(void*)-1&&m!=0,20))return 20;((char*)m)[0]='m';if(check(mprotect(m,4096,PROT_READ)==0&&mprotect(m,4096,PROT_READ|PROT_WRITE)==0&&((char*)m)[0]=='m',21))return 21;if(check(munmap(m,4096)==0,22))return 22;}
 if(check(getpid()>0,23))return 23;
 if(check(open("/etc/libos-does-not-exist",O_RDONLY)==-1&&errno==ENOENT,24))return 24;
 d=opendir("/bin");if(check(d!=0,25))return 25;while((e=readdir(d))!=0){if(e->d_ino==0||strlen(e->d_name)>=sizeof(e->d_name))return check(0,26);if(strcmp(e->d_name,"sh")==0){seen=1;if(e->d_type!=DT_REG)return check(0,27);}}if(check(closedir(d)==0&&seen,28))return 28;
 if(check(printf("libos printf: %s %d\n","ok",7)>0,29))return 29;
 if(check(puts("libos fixture: stdio ok")==0,30))return 30;
 {static const char marker[]="OpenSillicon: libos core, allocation, stdio, VFS, and directory APIs passed\n";if(check(write(STDOUT_FILENO,marker,sizeof(marker)-1)==(ssize_t)(sizeof(marker)-1),31))return 31;}
 return 0;
}
