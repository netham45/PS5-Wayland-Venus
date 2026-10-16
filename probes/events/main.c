#include "ps5log.h"
#include <sys/types.h>
#include <sys/event.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#ifndef NATIVE_ONLY
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <sys/signalfd.h>
#endif
#include <ps5/kernel.h>
#include <dlfcn.h>
/* Mirrors SDL release-2.30.x-ps5 SDL_ps5video.c:105 equeue wait;
 * SDL_ps5video.h:58-60 declares its queue/event ABI. */
int sceKernelCreateEqueue(struct kevent **,const char *);
int sceKernelAddReadEvent(struct kevent *,int,void *);
int sceKernelWaitEqueue(struct kevent *,struct kevent *,int,int *,unsigned *);
int sceKernelDeleteEqueue(struct kevent *);
#ifndef SHIM_ONLY
static int wakefd;
static struct kevent *scequeue;
static void *scewait(void *unused) {
    (void)unused; struct kevent e={0};int count=0;unsigned timeout=2000000;int r=sceKernelWaitEqueue(scequeue,&e,1,&count,&timeout);
    ps5log("sceKernelWaitEqueue thread rc=%#x count=%d ident=%lu filter=%d data=%ld",r,count,e.ident,e.filter,e.data);if(!r&&count==1)write(wakefd,"E",1);return NULL;
}
static void read_event(const char *what,int rd,int wr) {
    int q=kqueue();struct kevent e;EV_SET(&e,rd,EVFILT_READ,EV_ADD|EV_ONESHOT,0,0,NULL);errno=0;int r=kevent(q,&e,1,NULL,0,NULL);ps5log("kqueue EVFILT_READ %s add rc=%d errno=%d",what,r,r<0?errno:0);
    write(wr,"R",1);struct timespec timeout={1,0};r=kevent(q,NULL,0,&e,1,&timeout);ps5log("kqueue EVFILT_READ %s wait rc=%d errno=%d ident=%lu filter=%d flags=%x data=%ld",what,r,r<0?errno:0,e.ident,e.filter,e.flags,e.data);
    struct pollfd p={rd,POLLIN,0};r=poll(&p,1,1000);char c=0;ssize_t n=read(rd,&c,1);ps5log("poll %s rc=%d revents=%x read=%zd byte=%c",what,r,p.revents,n,c);close(q);
}
#endif
int main(void) {
    alarm(60);
    /* Block process-directed SIGUSR1 before the logger creates its thread. */
    sigset_t blocked;sigemptyset(&blocked);sigaddset(&blocked,SIGUSR1);
    pthread_sigmask(SIG_BLOCK,&blocked,NULL);
#ifdef SHIM_ONLY
    ps5log_init("probe-events-shim",9548);
#else
    ps5log_init("probe-events-native",9545);
#endif
    ps5log("preamble pid=%d firmware_raw=%#x",getpid(),kernel_get_fw_version());
    const char *symbols[]={"socketpair","pipe","kqueue","kevent","read","write","close","ppoll","poll","sceKernelAddReadEvent"};
    for(size_t i=0;i<sizeof(symbols)/sizeof(symbols[0]);i++)ps5log("runtime symbol %s=%p",symbols[i],dlsym(RTLD_DEFAULT,symbols[i]));
    sleep(3);
    int r;
#ifndef SHIM_ONLY
    ps5log("started pid=%d",getpid());int pair[2];r=socketpair(AF_UNIX,SOCK_STREAM,0,pair);ps5log("socketpair rc=%d errno=%d",r,r<0?errno:0);if(!r){read_event("AF_UNIX",pair[0],pair[1]);close(pair[0]);close(pair[1]);}
    r=pipe(pair);ps5log("pipe rc=%d errno=%d",r,r<0?errno:0);if(!r){read_event("pipe",pair[0],pair[1]);close(pair[0]);close(pair[1]);}
    int l=socket(AF_INET,SOCK_STREAM,0);struct sockaddr_in a={.sin_len=sizeof(a),.sin_family=AF_INET,.sin_addr.s_addr=htonl(INADDR_LOOPBACK)};r=bind(l,(struct sockaddr*)&a,sizeof(a));if(!r)r=listen(l,1);socklen_t len=sizeof(a);getsockname(l,(struct sockaddr*)&a,&len);int c=socket(AF_INET,SOCK_STREAM,0);if(!r)r=connect(c,(struct sockaddr*)&a,len);ps5log("TCP setup rc=%d errno=%d",r,r<0?errno:0);if(!r){int s=accept(l,NULL,NULL);read_event("TCP",s,c);close(s);}close(c);close(l);
    for(int kind=0;kind<2;kind++){int q=kqueue();struct kevent e;EV_SET(&e,42,kind?EVFILT_USER:EVFILT_TIMER,EV_ADD|EV_ONESHOT,0,kind?0:50,NULL);r=kevent(q,&e,1,NULL,0,NULL);ps5log("kqueue %s add rc=%d errno=%d",kind?"USER":"TIMER",r,r<0?errno:0);if(kind&&r==0){EV_SET(&e,42,EVFILT_USER,0,NOTE_TRIGGER,0,NULL);r=kevent(q,&e,1,NULL,0,NULL);ps5log("kqueue USER trigger rc=%d errno=%d",r,r<0?errno:0);}struct timespec t={1,0};r=kevent(q,NULL,0,&e,1,&t);ps5log("kqueue %s wait rc=%d errno=%d filter=%d flags=%x data=%ld",kind?"USER":"TIMER",r,r<0?errno:0,e.filter,e.flags,e.data);close(q);}
    int source[2],wake[2];socketpair(AF_UNIX,SOCK_STREAM,0,source);socketpair(AF_UNIX,SOCK_STREAM,0,wake);wakefd=wake[1];r=sceKernelCreateEqueue(&scequeue,"ps5wl events");ps5log("sceKernelCreateEqueue rc=%#x queue=%p",r,scequeue);if(!r){r=sceKernelAddReadEvent(scequeue,source[0],NULL);ps5log("sceKernelAddReadEvent rc=%#x",r);if(!r){pthread_t thread;int tr=pthread_create(&thread,NULL,scewait,NULL);ps5log("pthread_create rc=%d",tr);if(!tr){write(source[1],"S",1);struct pollfd p={wake[0],POLLIN,0};r=poll(&p,1,3000);ps5log("sce equeue -> thread -> socketpair poll rc=%d revents=%x",r,p.revents);pthread_join(thread,NULL);}}ps5log("sceKernelDeleteEqueue rc=%#x",sceKernelDeleteEqueue(scequeue));}close(source[0]);close(source[1]);close(wake[0]);close(wake[1]);
#endif
#ifndef NATIVE_ONLY
    ps5log("CALL epoll_create1");sleep(1);
    int ep=epoll_create1(EPOLL_CLOEXEC);ps5log("epoll_create1 fd=%d errno=%d",ep,ep<0?errno:0);
    for(int kind=0;kind<3;kind++){
        sigset_t mask;sigemptyset(&mask);sigaddset(&mask,SIGUSR1);if(kind==2)pthread_sigmask(SIG_BLOCK,&mask,NULL);
        int fd=kind==0?eventfd(0,EFD_CLOEXEC|EFD_NONBLOCK):kind==1?timerfd_create(CLOCK_MONOTONIC,TFD_CLOEXEC|TFD_NONBLOCK):signalfd(-1,&mask,SFD_CLOEXEC|SFD_NONBLOCK);
        ps5log("shim %s create fd=%d errno=%d",kind==0?"eventfd":kind==1?"timerfd":"signalfd",fd,fd<0?errno:0);if(fd<0)continue;
        struct epoll_event event={.events=EPOLLIN,.data.u32=kind};r=epoll_ctl(ep,EPOLL_CTL_ADD,fd,&event);ps5log("shim kind=%d epoll_ctl rc=%d errno=%d",kind,r,r<0?errno:0);
        if(kind==0){uint64_t v=7;r=write(fd,&v,8);}else if(kind==1){struct itimerspec t={.it_value={0,50000000}};r=timerfd_settime(fd,0,&t,NULL);}else r=kill(getpid(),SIGUSR1);
        ps5log("shim kind=%d signal rc=%d errno=%d",kind,r,r<0?errno:0);r=epoll_wait(ep,&event,1,2000);ps5log("shim kind=%d epoll_wait rc=%d errno=%d events=%x",kind,r,r<0?errno:0,event.events);
        if(r==1){unsigned char b[128]={0};ssize_t n=read(fd,b,kind==2?sizeof(b):8);uint64_t value=0;memcpy(&value,b,8);ps5log("shim kind=%d read=%zd first_u64=%lu",kind,n,value);}close(fd);
    }
    sigset_t temporary,after;sigemptyset(&temporary);struct epoll_event none={0};
    errno=0;r=epoll_pwait(ep,&none,1,0,&temporary);int saved=errno;
    int maskrc=pthread_sigmask(SIG_SETMASK,NULL,&after);
    ps5log("epoll_pwait temporary empty mask rc=%d errno=%d query_rc=%d SIGUSR1_restored_blocked=%d",r,saved,maskrc,sigismember(&after,SIGUSR1));
    close(ep);
#endif
    ps5log("events probe complete");ps5log_flush_wait(20000);return 0;
}
