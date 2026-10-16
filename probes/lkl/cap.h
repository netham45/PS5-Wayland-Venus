#ifndef LKLCAP_H
#define LKLCAP_H
#include "ps5log.h"
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#define PAGE 16384UL
#define DIR "/data/ps5wl/lklcap"
#define SOCK "/system_tmp/ps5wl-lklcap.sock"
static int failures;
#define CHECK(name,ok,fmt,...) do { int check_passed=!!(ok); ps5log("%s %s " fmt,check_passed?"PASS":"FAIL",name,##__VA_ARGS__); if(!check_passed) failures++; } while(0)
/* Negative errno avoids touching libc TLS in raw-created threads. */
static __attribute__((always_inline)) inline long raw6(long n,long a,long b,long c,long d,long e,long f) {
    register long r10 __asm__("r10")=d,r8 __asm__("r8")=e,r9 __asm__("r9")=f;
    unsigned char bad; long out;
    __asm__ volatile("syscall; setc %1":"=a"(out),"=qm"(bad):"a"(n),"D"(a),"S"(b),"d"(c),"r"(r10),"r"(r8),"r"(r9):"rcx","r11","memory");
    return bad?-out:out;
}
#define RAW(n,a,b,c,d,e) raw6(n,(long)(a),(long)(b),(long)(c),(long)(d),(long)(e),0)
static double now(void) {struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
static volatile sig_atomic_t owned_child;
static void deadline(int sig) {(void)sig;if(owned_child>0)kill(owned_child,SIGKILL);static const char line[]="EXIT 124 watchdog\n";ps5log_emergency_write(line,sizeof(line)-1);_exit(124);}
static void begin(const char *name,int port) {signal(SIGALRM,deadline);signal(SIGPIPE,SIG_IGN);signal(SIGSYS,SIG_IGN);alarm(55);ps5log_init(name,port);ps5log("BEGIN %s pid=%d ppid=%d uid=%d euid=%d page=%d",name,getpid(),getppid(),getuid(),geteuid(),getpagesize());}
static int finish(void) {ps5log("EXIT %d failures=%d",failures?1:0,failures);ps5log_flush_wait(2500);alarm(0);return failures?1:0;}
static int ready(int fd,int ms) {struct pollfd p={fd,POLLIN,0};return poll(&p,1,ms)>0;}
static int full(int fd,void *p,size_t n,int sending) {while(n){ssize_t r=sending?write(fd,p,n):read(fd,p,n);if(r<0&&errno==EINTR)continue;if(r<=0)return -1;p=(char*)p+r;n-=r;}return 0;}
static int sendfd(int s,int fd,int mode) {char control[CMSG_SPACE(sizeof(int))]={0};struct iovec i={&mode,sizeof(mode)};struct msghdr m={.msg_iov=&i,.msg_iovlen=1,.msg_control=control,.msg_controllen=sizeof(control)};struct cmsghdr *c=CMSG_FIRSTHDR(&m);c->cmsg_level=SOL_SOCKET;c->cmsg_type=SCM_RIGHTS;c->cmsg_len=CMSG_LEN(sizeof(int));memcpy(CMSG_DATA(c),&fd,sizeof(fd));return sendmsg(s,&m,0)==sizeof(mode)?0:-1;}
static int helper(int fd,int mode,pid_t *pid) {
    unlink(SOCK);int l=socket(AF_UNIX,SOCK_STREAM,0);struct sockaddr_un a={.sun_len=sizeof(a),.sun_family=AF_UNIX};strcpy(a.sun_path,SOCK);
    if(l<0||bind(l,(void*)&a,sizeof(a))||listen(l,1)){if(l>=0)close(l);return -1;}
    int s=socket(AF_INET,SOCK_STREAM,0);struct sockaddr_in in={.sin_len=sizeof(in),.sin_family=AF_INET,.sin_port=htons(9021),.sin_addr.s_addr=htonl(INADDR_LOOPBACK)};
    char uri[]="file:/data/pldmgr/payloads/ps5wl-lklcap-helper/ps5wl-lklcap-helper.elf?pipe=0\n";
    if(s<0||connect(s,(void*)&in,sizeof(in))||full(s,uri,sizeof(uri)-1,1)){if(s>=0)close(s);close(l);unlink(SOCK);return -1;}
    shutdown(s,SHUT_WR);close(s);int c=-1;if(ready(l,8000))c=accept(l,0,0);close(l);unlink(SOCK);
    if(c<0)return -1;if(!ready(c,2000)||full(c,pid,sizeof(*pid),0)||sendfd(c,fd,mode)){close(c);return -1;}return c;
}
static __attribute__((always_inline)) inline pid_t clone_raw(int call,int flags) {
    long a=call,d;unsigned char bad;
    __asm__ volatile("syscall; setc %2":"+a"(a),"=d"(d),"=qm"(bad):"D"((long)flags):"rcx","r11","memory");
    if(bad){errno=a;return -1;}return d?0:(pid_t)a;
}
static int reap(pid_t p,int *st,int ms) {double end=now()+ms/1000.;while(now()<end){pid_t r=waitpid(p,st,WNOHANG|WUNTRACED);if(r==p)return 0;if(r<0)return -1;usleep(1000);}errno=ETIMEDOUT;return -1;}
static void retire(pid_t p) {if(p>0){kill(p,SIGKILL);int st;reap(p,&st,2000);}owned_child=0;}
#endif
