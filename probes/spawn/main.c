#include "ps5log.h"
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
/* elfldr master socksrv.c:243-257 accepts newline file: URI or raw ELF;
 * this probe implements a sender, not a copy of the loader. */
#define CHILD_PATH "/data/pldmgr/payloads/ps5wl-probe-spawn-child/ps5wl-probe-spawn-child.elf"
static int connect_port(int port) {
    int s=socket(AF_INET,SOCK_STREAM,0); struct sockaddr_in a={.sin_len=sizeof(a),.sin_family=AF_INET,.sin_port=htons(port),.sin_addr.s_addr=htonl(INADDR_LOOPBACK)};
    if(connect(s,(struct sockaddr*)&a,sizeof(a))<0) {ps5log("connect port=%d errno=%d",port,errno);close(s);return -1;} return s;
}
static int sendall(int s,const void *p,size_t n) { while(n) {ssize_t r=write(s,p,n);if(r<0&&errno==EINTR)continue;if(r<=0)return -1;p=(const char*)p+r;n-=r;}return 0; }
int main(void) {
    alarm(60);
#ifdef CHILD
    ps5log_init("probe-spawn-child",9543); ps5log("child started pid=%d ppid=%d uid=%u",getpid(),getppid(),getuid());
    int s=connect_port(9544); if(s>=0) {char b[128];int n=snprintf(b,sizeof(b),"child pid=%d ppid=%d uid=%u",getpid(),getppid(),getuid());sendall(s,b,n);close(s);} ps5log_flush_wait(1000);
#else
    ps5log_init("probe-spawn-parent",9542); ps5log("parent started pid=%d ppid=%d uid=%u child_path=%s",getpid(),getppid(),getuid(),CHILD_PATH);
    int listener=socket(AF_INET,SOCK_STREAM,0); struct sockaddr_in a={.sin_len=sizeof(a),.sin_family=AF_INET,.sin_port=htons(9544),.sin_addr.s_addr=htonl(INADDR_LOOPBACK)};
    if(bind(listener,(struct sockaddr*)&a,sizeof(a))<0||listen(listener,2)<0){ps5log("callback bind/listen errno=%d",errno);goto done;}
    for(int mode=0;mode<2;mode++) {
        int file=open(CHILD_PATH,O_RDONLY);ps5log("child file open mode=%s fd=%d errno=%d",mode?"raw":"file URI",file,file<0?errno:0);if(file<0)continue;
        int s=connect_port(9021);if(s<0){close(file);continue;}
        int rc=0; size_t bytes=0;
        if(!mode){char b[512];int n=snprintf(b,sizeof(b),"file:%s?pipe=0\n",CHILD_PATH);rc=sendall(s,b,n);bytes=n;}
        else {char b[16384];ssize_t n;while((n=read(file,b,sizeof(b)))>0){if(sendall(s,b,n)<0){rc=-1;break;}bytes+=n;}if(n<0)rc=-1;}
        close(file);shutdown(s,SHUT_WR);ps5log("elfldr send mode=%s bytes=%zu rc=%d errno=%d",mode?"raw":"file URI",bytes,rc,rc<0?errno:0);
        struct pollfd p={listener,POLLIN,0};int r=poll(&p,1,15000);
        if(r>0){int c=accept(listener,NULL,NULL);char b[256]={0};ssize_t n=read(c,b,sizeof(b)-1);ps5log("spawn mode=%s callback bytes=%zd parent_pid=%d %s",mode?"raw":"file URI",n,getpid(),b);close(c);}else ps5log("spawn mode=%s callback FAIL poll=%d errno=%d",mode?"raw":"file URI",r,r<0?errno:0);
        close(s);sleep(2);
    }
done:close(listener);ps5log("spawn probe complete");ps5log_flush_wait(20000);
#endif
    return 0;
}
