#include "ps5log.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/ucred.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include <sys/sysctl.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* etaHEN main Source Code/daemon/source/msg.cpp:477-495 demonstrates
 * AF_UNIX bind/listen. SCM_RIGHTS and 32 MiB coherence are tested here. */
#define ENDPOINT "/system_tmp/ps5wl-probe.sock"
#define LENGTH (32u * 1024u * 1024u)
static void credentials(int s) {
    uid_t uid = -1; gid_t gid = -1;
    int (*getpeer)(int,uid_t*,gid_t*)=dlsym(RTLD_DEFAULT,"getpeereid");
    errno = 0; int r = getpeer ? getpeer(s, &uid, &gid) : -1;
    ps5log("getpeereid symbol=%p rc=%d errno=%d uid=%u gid=%u", getpeer, r, errno, uid, gid);
    struct xucred cred = {0}; socklen_t n = sizeof(cred);
    errno = 0; r = getsockopt(s, 0, LOCAL_PEERCRED, &cred, &n);
    ps5log("LOCAL_PEERCRED rc=%d errno=%d size=%u version=%u uid=%u ngroups=%d", r, errno, n, cred.cr_version, cred.cr_uid, cred.cr_ngroups);
}
static int transfer(int s, void *p, size_t n, int sending) {
    while (n) { ssize_t r = sending ? write(s,p,n) : read(s,p,n); if (r < 0 && errno == EINTR) continue; if (r <= 0) return -1; p = (char *)p+r; n -= r; } return 0;
}
static int sendfd(int s, int fd, unsigned char kind) {
    char control[CMSG_SPACE(sizeof(int))] = {0};
    struct iovec iov = {&kind, 1};
    struct msghdr msg = {.msg_iov=&iov,.msg_iovlen=1,.msg_control=control,.msg_controllen=sizeof(control)};
    struct cmsghdr *c=CMSG_FIRSTHDR(&msg); c->cmsg_level=SOL_SOCKET; c->cmsg_type=SCM_RIGHTS; c->cmsg_len=CMSG_LEN(sizeof(fd)); memcpy(CMSG_DATA(c),&fd,sizeof(fd));
    return sendmsg(s,&msg,0)==1 ? 0 : -1;
}
static int recvfd(int s, unsigned char *kind) {
    char control[CMSG_SPACE(sizeof(int))] = {0}; struct iovec iov={kind,1};
    struct msghdr msg={.msg_iov=&iov,.msg_iovlen=1,.msg_control=control,.msg_controllen=sizeof(control)};
    errno=0; int r=recvmsg(s,&msg,MSG_CMSG_CLOEXEC); int err=errno;
    if(r<0&&err==EINVAL){ps5log("recvmsg MSG_CMSG_CLOEXEC rejected errno=%d; retrying without flag",err);msg.msg_controllen=sizeof(control);msg.msg_flags=0;errno=0;r=recvmsg(s,&msg,0);err=errno;}
    struct cmsghdr *c=CMSG_FIRSTHDR(&msg); int fd=-1;
    if(r==1 && !(msg.msg_flags&MSG_CTRUNC) && c && c->cmsg_level==SOL_SOCKET && c->cmsg_type==SCM_RIGHTS && c->cmsg_len==CMSG_LEN(sizeof(fd))) memcpy(&fd,CMSG_DATA(c),sizeof(fd));
    ps5log("recvmsg MSG_CMSG_CLOEXEC rc=%d errno=%d flags=%x kind=%u fd=%d FD_CLOEXEC=%d",r,err,msg.msg_flags,*kind,fd,fd<0?-1:!!(fcntl(fd,F_GETFD)&FD_CLOEXEC)); return fd;
}
int main(void) {
#ifdef SERVER
    ps5log_init("probe-ipc-server",9540);
#else
    ps5log_init("probe-ipc-client",9541);
#endif
    alarm(90);
    ps5log("started pid=%d ppid=%d uid=%u pagesize=%ld",getpid(),getppid(),getuid(),sysconf(_SC_PAGESIZE));
    char os[128]={0}; size_t oslen=sizeof(os); int osrc=sysctlbyname("kern.osrelease",os,&oslen,NULL,0); ps5log("kern.osrelease rc=%d value=%s (not console firmware)",osrc,os);
    int s=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0); ps5log("socket SOCK_CLOEXEC fd=%d errno=%d FD_CLOEXEC=%d",s,s<0?errno:0,s<0?-1:!!(fcntl(s,F_GETFD)&FD_CLOEXEC));
    if(s<0) goto out;
    struct timeval tv={30,0}; setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv)); setsockopt(s,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof(tv));
    struct sockaddr_un a={.sun_family=AF_UNIX}; strcpy(a.sun_path,ENDPOINT); a.sun_len=offsetof(struct sockaddr_un,sun_path)+strlen(ENDPOINT)+1;
#ifdef SERVER
    unlink(ENDPOINT); int r=bind(s,(struct sockaddr*)&a,a.sun_len); ps5log("bind %s rc=%d errno=%d",ENDPOINT,r,r<0?errno:0); if(r<0) goto closeout;
    r=listen(s,2); ps5log("listen rc=%d errno=%d",r,r<0?errno:0); if(r<0) goto closeout;
    struct pollfd p={s,POLLIN,0}; r=poll(&p,1,45000); ps5log("poll accept rc=%d revents=%x",r,p.revents); if(r<=0) goto closeout;
    int (*accept_cloexec)(int,struct sockaddr*,socklen_t*,int)=dlsym(RTLD_DEFAULT,"accept4");
    int peer=accept_cloexec?accept_cloexec(s,NULL,NULL,SOCK_CLOEXEC):-1;
    ps5log("accept4 symbol=%p fd=%d errno=%d",accept_cloexec,peer,peer<0?errno:0);
    if(peer<0){peer=accept(s,NULL,NULL);ps5log("accept fallback fd=%d errno=%d",peer,peer<0?errno:0);} if(peer<0) goto closeout; close(s); s=peer;
    credentials(s);
    for(int i=0;i<3;i++) {
        uint32_t length=0;if(transfer(s,&length,sizeof(length),0)<0)break;
        unsigned char kind=255; int fd=recvfd(s,&kind); if(fd<0) break;
        unsigned char *m=mmap(NULL,length,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0); ps5log("server mmap kind=%u length=%u ptr=%p errno=%d",kind,length,m,m==MAP_FAILED?errno:0);
        unsigned char ok=m!=MAP_FAILED;
        if(ok) { for(size_t k=0;k<length;k++) if(m[k]!=(unsigned char)(k*17+kind)) {ok=0;ps5log("mismatch offset=%zu got=%u",k,m[k]);break;} for(size_t k=0;k<length;k++) m[k]=(unsigned char)(k*29+kind+7); munmap(m,length); }
        close(fd); ps5log("server coherence client->server kind=%u all_%u_bytes=%s",kind,length,ok?"PASS":"FAIL"); if(transfer(s,&ok,1,1)<0) break;
        unsigned char reverse=0; if(transfer(s,&reverse,1,0)<0) break; ps5log("server coherence server->client kind=%u verdict=%s",kind,reverse?"PASS":"FAIL");
    }
#else
    int r=connect(s,(struct sockaddr*)&a,a.sun_len); ps5log("connect rc=%d errno=%d",r,r<0?errno:0); if(r<0) goto closeout; credentials(s);
    for(unsigned char kind=0;kind<3;kind++) {
        const char *name=kind==1?"/ps5wl-probe-shm":"/system_tmp/ps5wl-probe-map";
        int fd=kind==0?shm_open(SHM_ANON,O_RDWR,0600):kind==1?shm_open(name,O_CREAT|O_EXCL|O_RDWR,0600):open(name,O_CREAT|O_TRUNC|O_RDWR,0600);
        ps5log("create kind=%u (%s) fd=%d errno=%d",kind,kind==0?"SHM_ANON":name,fd,fd<0?errno:0);
        if(fd<0) {sendfd(s,-1,kind);break;}
        uint32_t length=LENGTH;
        r=ftruncate(fd,length); ps5log("ftruncate kind=%u length=%u rc=%d errno=%d",kind,length,r,r<0?errno:0);
        if(r<0&&kind==2){length=16384;r=ftruncate(fd,length);ps5log("ftruncate smaller-file kind=%u length=%u rc=%d errno=%d",kind,length,r,r<0?errno:0);}if(r<0){close(fd);if(kind==2)unlink(name);break;}
        unsigned char *m=mmap(NULL,length,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0); ps5log("client mmap kind=%u ptr=%p errno=%d",kind,m,m==MAP_FAILED?errno:0); if(m==MAP_FAILED) {close(fd);break;}
        for(size_t k=0;k<length;k++) m[k]=(unsigned char)(k*17+kind);
        if(transfer(s,&length,sizeof(length),1)<0){munmap(m,length);close(fd);break;}
        r=sendfd(s,fd,kind); ps5log("sendmsg SCM_RIGHTS kind=%u rc=%d errno=%d",kind,r,r<0?errno:0);
        unsigned char ack=0,ok=1; if(r<0 || transfer(s,&ack,1,0)<0) ok=0;
        if(ok) for(size_t k=0;k<length;k++) if(m[k]!=(unsigned char)(k*29+kind+7)) {ok=0;ps5log("reverse mismatch offset=%zu",k);break;}
        ps5log("client coherence server->client kind=%u all_%u_bytes=%s server_ack=%u",kind,length,ok?"PASS":"FAIL",ack); transfer(s,&ok,1,1);
        munmap(m,length); close(fd); if(kind==1) shm_unlink(name); if(kind==2) unlink(name);
    }
#endif
closeout: close(s);
#ifdef SERVER
    unlink(ENDPOINT);
#endif
out: ps5log("probe complete"); ps5log_flush_wait(20000); return 0;
}
