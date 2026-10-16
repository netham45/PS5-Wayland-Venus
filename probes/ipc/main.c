#include "ps5log.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/ucred.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include <sys/sysctl.h>
#include <sys/stat.h>
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
#ifdef EXACT_DUP
#define ENDPOINT "/system_tmp/ps5wl-probe-exact.sock"
#define LENGTH 8294400u
#else
#define ENDPOINT "/system_tmp/ps5wl-probe.sock"
#define LENGTH (32u * 1024u * 1024u)
#endif
#ifdef EXACT_DUP
static void inspect_fd(const char *where,int fd,size_t length){
    struct stat st={0};errno=0;int rc=fstat(fd,&st);int err=errno;
    int flags=fcntl(fd,F_GETFL,0),fdflags=fcntl(fd,F_GETFD,0);
    ps5log("fd inspect %s fd=%d fstat=%d errno=%d mode=%#o size=%lld dev=%llu ino=%llu F_GETFL=%#x F_GETFD=%#x",where,fd,rc,err,st.st_mode,(long long)st.st_size,(unsigned long long)st.st_dev,(unsigned long long)st.st_ino,flags,fdflags);
    for(int mode=0;mode<4;mode++){
        int prot=PROT_READ|(mode&1?PROT_WRITE:0),mapflags=mode&2?MAP_PRIVATE:MAP_SHARED;errno=0;
        void *p=mmap(NULL,length,prot,mapflags,fd,0);err=errno;
        ps5log("map inspect %s fd=%d length=%zu prot=%#x flags=%#x ptr=%p errno=%d",where,fd,length,prot,mapflags,p,p==MAP_FAILED?err:0);
        if(p!=MAP_FAILED)munmap(p,length);
    }
}
static void probe_creation_flags(void){
    int fd=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK,0);
    ps5log("flag control socket fd=%d errno=%d FD_CLOEXEC=%d O_NONBLOCK=%d",fd,fd<0?errno:0,fd<0?-1:!!(fcntl(fd,F_GETFD,0)&FD_CLOEXEC),fd<0?-1:!!(fcntl(fd,F_GETFL,0)&O_NONBLOCK));if(fd>=0)close(fd);
    int pair[2]={-1,-1};errno=0;int rc=socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK,0,pair);
    ps5log("flag control socketpair rc=%d errno=%d",rc,rc<0?errno:0);
    if(!rc)for(int i=0;i<2;i++){ps5log("flag control socketpair end=%d FD_CLOEXEC=%d O_NONBLOCK=%d",i,!!(fcntl(pair[i],F_GETFD,0)&FD_CLOEXEC),!!(fcntl(pair[i],F_GETFL,0)&O_NONBLOCK));close(pair[i]);}
    fd=shm_open(SHM_ANON,O_RDWR|O_CLOEXEC,0600);ps5log("flag control shm_open O_CLOEXEC fd=%d errno=%d FD_CLOEXEC=%d",fd,fd<0?errno:0,fd<0?-1:!!(fcntl(fd,F_GETFD,0)&FD_CLOEXEC));if(fd>=0)close(fd);
    char path[96];snprintf(path,sizeof(path),"/system_tmp/ps5wl-probe-flags-%d",getpid());
    fd=open(path,O_RDWR|O_CREAT|O_EXCL|O_CLOEXEC,0600);ps5log("flag control open O_CLOEXEC fd=%d errno=%d FD_CLOEXEC=%d",fd,fd<0?errno:0,fd<0?-1:!!(fcntl(fd,F_GETFD,0)&FD_CLOEXEC));if(fd>=0){close(fd);unlink(path);}
}
static void probe_exact_target_duplication(void){
    ps5log("native dup2 symbol=%p dup3 symbol=%p",dlsym(RTLD_DEFAULT,"dup2"),dlsym(RTLD_DEFAULT,"dup3"));
    for(int mode=0;mode<2;mode++){
        int source=shm_open(SHM_ANON,O_RDWR|O_CLOEXEC,0600);
        if(source<0){ps5log("exact-target mode=%d source create errno=%d",mode,errno);continue;}
        if(ftruncate(source,16384)<0){ps5log("exact-target mode=%d ftruncate errno=%d",mode,errno);close(source);continue;}
        char path[96];snprintf(path,sizeof(path),"/system_tmp/ps5wl-probe-dup-%d-%d",getpid(),mode);
        int target=open(path,O_RDWR|O_CREAT|O_EXCL,0600);
        if(target<0){ps5log("exact-target mode=%d target create errno=%d",mode,errno);close(source);continue;}
        struct stat before={0},after={0};int srcstat=fstat(source,&before);
        errno=0;int result=mode?dup3(source,target,O_CLOEXEC):fcntl(source,F_DUP2FD_CLOEXEC,target);int error=errno;
        int dststat=fstat(target,&after),fdflags=fcntl(target,F_GETFD,0);
        int identity=!srcstat&&!dststat&&before.st_dev==after.st_dev&&before.st_ino==after.st_ino&&before.st_mode==after.st_mode&&before.st_size==after.st_size;
        void *a=mmap(NULL,16384,PROT_READ|PROT_WRITE,MAP_SHARED,source,0),*b=mmap(NULL,16384,PROT_READ|PROT_WRITE,MAP_SHARED,target,0);
        int coherent=0;if(a!=MAP_FAILED&&b!=MAP_FAILED){*(volatile unsigned char*)a=0x5a;coherent=*(volatile unsigned char*)b==0x5a;*(volatile unsigned char*)b=0xa5;coherent=coherent&&*(volatile unsigned char*)a==0xa5;}
        ps5log("exact-target api=%s source=%d target=%d result=%d errno=%d srcstat=%d dststat=%d mode=%#o size=%lld FD_CLOEXEC=%d identity=%d coherent=%d verdict=%s",mode?"dup3":"F_DUP2FD_CLOEXEC",source,target,result,error,srcstat,dststat,after.st_mode,(long long)after.st_size,fdflags<0?-1:!!(fdflags&FD_CLOEXEC),identity,coherent,result==target&&identity&&coherent&&fdflags>=0&&(fdflags&FD_CLOEXEC)?"PASS":"FAIL");
        if(a!=MAP_FAILED)munmap(a,16384);if(b!=MAP_FAILED)munmap(b,16384);
        close(target);close(source);unlink(path);
    }
}
#endif
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
#if defined(EXACT_DUP) && defined(SERVER)
    ps5log_init("probe-ipc-exact-server",9549);
#elif defined(EXACT_DUP)
    ps5log_init("probe-ipc-exact-client",9550);
#elif defined(SERVER)
    ps5log_init("probe-ipc-server",9540);
#else
    ps5log_init("probe-ipc-client",9541);
#endif
    alarm(90);
    ps5log("started pid=%d ppid=%d uid=%u pagesize=%ld",getpid(),getppid(),getuid(),sysconf(_SC_PAGESIZE));
#ifdef EXACT_DUP
    probe_creation_flags();
    probe_exact_target_duplication();
#endif
    char os[128]={0}; size_t oslen=sizeof(os); int osrc=sysctlbyname("kern.osrelease",os,&oslen,NULL,0); ps5log("kern.osrelease rc=%d value=%s (not console firmware)",osrc,os);
    int s=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0); ps5log("socket SOCK_CLOEXEC fd=%d errno=%d FD_CLOEXEC=%d",s,s<0?errno:0,s<0?-1:!!(fcntl(s,F_GETFD)&FD_CLOEXEC));
    if(s<0) goto out;
    struct timeval tv={30,0}; setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv)); setsockopt(s,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof(tv));
    struct sockaddr_un a={.sun_family=AF_UNIX}; strcpy(a.sun_path,ENDPOINT); a.sun_len=offsetof(struct sockaddr_un,sun_path)+strlen(ENDPOINT)+1;
#ifdef SERVER
    unlink(ENDPOINT); int r=bind(s,(struct sockaddr*)&a,a.sun_len); ps5log("bind %s rc=%d errno=%d",ENDPOINT,r,r<0?errno:0); if(r<0) goto closeout;
    r=listen(s,2); ps5log("listen rc=%d errno=%d",r,r<0?errno:0); if(r<0) goto closeout;
    struct pollfd p={s,POLLIN,0}; r=poll(&p,1,45000); ps5log("poll accept rc=%d revents=%x",r,p.revents); if(r<=0) goto closeout;
    void *native_accept4=dlsym(RTLD_DEFAULT,"accept4");
    int peer=accept4(s,NULL,NULL,SOCK_CLOEXEC|SOCK_NONBLOCK),accept_error=peer<0?errno:0;
    int peer_fd_flags=peer<0?-1:fcntl(peer,F_GETFD),peer_status_flags=peer<0?-1:fcntl(peer,F_GETFL);
    ps5log("accept4 native_symbol=%p fd=%d errno=%d FD_CLOEXEC=%d O_NONBLOCK=%d",native_accept4,peer,accept_error,peer_fd_flags<0?-1:!!(peer_fd_flags&FD_CLOEXEC),peer_status_flags<0?-1:!!(peer_status_flags&O_NONBLOCK));
    if(peer<0)goto closeout;
    if(peer_status_flags<0||fcntl(peer,F_SETFL,peer_status_flags&~O_NONBLOCK)<0){ps5log("restore accepted blocking mode errno=%d",errno);close(peer);goto closeout;}
    close(s);s=peer;
    credentials(s);
    for(int i=0;i<3;i++) {
        uint32_t length=0;if(transfer(s,&length,sizeof(length),0)<0)break;
        unsigned char kind=255; int fd=recvfd(s,&kind); if(fd<0) break;
#ifdef EXACT_DUP
        inspect_fd("server received",fd,length);
#endif
        unsigned char *m=mmap(NULL,length,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0); ps5log("server mmap kind=%u length=%u ptr=%p errno=%d",kind,length,m,m==MAP_FAILED?errno:0);
        unsigned char ok=m!=MAP_FAILED;
        if(ok) { for(size_t k=0;k<length;k++) if(m[k]!=(unsigned char)(k*17+kind)) {ok=0;ps5log("mismatch offset=%zu got=%u",k,m[k]);break;} for(size_t k=0;k<length;k++) m[k]=(unsigned char)(k*29+kind+7); munmap(m,length); }
        close(fd); ps5log("server coherence client->server kind=%u all_%u_bytes=%s",kind,length,ok?"PASS":"FAIL"); if(transfer(s,&ok,1,1)<0) break;
        unsigned char reverse=0; if(transfer(s,&reverse,1,0)<0) break; ps5log("server coherence server->client kind=%u verdict=%s",kind,reverse?"PASS":"FAIL");
    }
#else
    int r=connect(s,(struct sockaddr*)&a,a.sun_len); ps5log("connect rc=%d errno=%d",r,r<0?errno:0); if(r<0) goto closeout; credentials(s);
    for(unsigned char kind=0;kind<3;kind++) {
#ifdef EXACT_DUP
        const char *name="SHM_ANON";int fd=shm_open(SHM_ANON,O_RDWR|O_CLOEXEC,0600);
        ps5log("create exact kind=%u %s fd=%d errno=%d",kind,name,fd,fd<0?errno:0);
#else
        const char *name=kind==1?"/ps5wl-probe-shm":"/system_tmp/ps5wl-probe-map";
        int fd=kind==0?shm_open(SHM_ANON,O_RDWR,0600):kind==1?shm_open(name,O_CREAT|O_EXCL|O_RDWR,0600):open(name,O_CREAT|O_TRUNC|O_RDWR,0600);
        ps5log("create kind=%u (%s) fd=%d errno=%d",kind,kind==0?"SHM_ANON":name,fd,fd<0?errno:0);
#endif
        if(fd<0) {sendfd(s,-1,kind);break;}
        uint32_t length=LENGTH;
        r=ftruncate(fd,length); ps5log("ftruncate kind=%u length=%u rc=%d errno=%d",kind,length,r,r<0?errno:0);
#ifndef EXACT_DUP
        if(r<0&&kind==2){length=16384;r=ftruncate(fd,length);ps5log("ftruncate smaller-file kind=%u length=%u rc=%d errno=%d",kind,length,r,r<0?errno:0);}
#endif
        if(r<0){close(fd);break;}
#ifdef EXACT_DUP
        fcntl(fd,F_SETFD,FD_CLOEXEC);inspect_fd("client original",fd,length);
#endif
        unsigned char *m=mmap(NULL,length,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0); ps5log("client mmap kind=%u ptr=%p errno=%d",kind,m,m==MAP_FAILED?errno:0); if(m==MAP_FAILED) {close(fd);break;}
        for(size_t k=0;k<length;k++) m[k]=(unsigned char)(k*17+kind);
#ifdef EXACT_DUP
        if(kind){
            int original=fd;errno=0;
            fd=fcntl(original,kind==1?F_DUPFD:F_DUPFD_CLOEXEC,0);
            ps5log("dup exact kind=%u original=%d duplicate=%d errno=%d",kind,original,fd,fd<0?errno:0);
            if(kind==2&&fd<0){errno=0;fd=fcntl(original,F_DUPFD,0);ps5log("dup exact fallback original=%d duplicate=%d errno=%d",original,fd,fd<0?errno:0);}
            if(fd>=0)fcntl(fd,F_SETFD,FD_CLOEXEC);
            close(original);ps5log("closed original before SCM_RIGHTS original=%d duplicate=%d",original,fd);
            if(fd<0){munmap(m,length);break;}
            inspect_fd("client duplicate original closed",fd,length);
        }
#endif
        if(transfer(s,&length,sizeof(length),1)<0){munmap(m,length);close(fd);break;}
        r=sendfd(s,fd,kind); ps5log("sendmsg SCM_RIGHTS kind=%u rc=%d errno=%d",kind,r,r<0?errno:0);
        unsigned char ack=0,ok=1; if(r<0 || transfer(s,&ack,1,0)<0) ok=0;
        if(ok) for(size_t k=0;k<length;k++) if(m[k]!=(unsigned char)(k*29+kind+7)) {ok=0;ps5log("reverse mismatch offset=%zu",k);break;}
        ps5log("client coherence server->client kind=%u all_%u_bytes=%s server_ack=%u",kind,length,ok?"PASS":"FAIL",ack); transfer(s,&ok,1,1);
        munmap(m,length); close(fd);
#ifndef EXACT_DUP
        if(kind==1) shm_unlink(name); if(kind==2) unlink(name);
#endif
    }
#endif
closeout: close(s);
#ifdef SERVER
    unlink(ENDPOINT);
#endif
out: ps5log("probe complete"); ps5log_flush_wait(20000); return 0;
}
