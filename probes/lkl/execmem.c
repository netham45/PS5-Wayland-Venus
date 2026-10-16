/* Native equivalents of freebsd_translation_memory/enforcement and shared_instruction. */
#include "cap.h"
#include <dlfcn.h>
#include <setjmp.h>
static sigjmp_buf jump;
static volatile int caught;
static void trap(int s){caught=s;siglongjmp(jump,1);}
static int execute(void *p){caught=0;if(sigsetjmp(jump,1)==0)return ((int(*)(void))p)();return -caught;}
int main(void){begin("lklcap-execmem",9600);signal(SIGSEGV,trap);signal(SIGBUS,trap);signal(SIGILL,trap);
    unsigned char code[]={0xb8,42,0,0,0,0xc3};
    void *p=mmap(0,PAGE,3,MAP_ANON|MAP_PRIVATE,-1,0);int r=-1,v=-1;
    if(p!=MAP_FAILED){memcpy(p,code,sizeof(code));r=mprotect(p,PAGE,PROT_READ|PROT_EXEC);if(!r)v=execute(p);}
    CHECK("anon_RW_to_RX",r==0&&v==42,"mprotect=%d errno=%d value=%d signal=%d",r,r<0?errno:0,v,caught);if(p!=MAP_FAILED)munmap(p,PAGE);
    p=mmap(0,PAGE,7,MAP_ANON|MAP_PRIVATE,-1,0);int err=errno;v=-1;if(p!=MAP_FAILED){memcpy(p,code,sizeof(code));v=execute(p);}
    CHECK("anon_RWX",p!=MAP_FAILED&&v==42,"map=%p errno=%d value=%d signal=%d",p,p==MAP_FAILED?err:0,v,caught);if(p!=MAP_FAILED)munmap(p,PAGE);
    int fd=shm_open(SHM_ANON,O_RDWR,0600);r=fd<0?-1:ftruncate(fd,PAGE);p=r?MAP_FAILED:mmap(0,PAGE,3,MAP_SHARED,fd,0);
    CHECK("shm_RW",p!=MAP_FAILED,"fd=%d map=%p errno=%d",fd,p,p==MAP_FAILED?errno:0);
    if(p!=MAP_FAILED){memcpy(p,code,sizeof(code));pid_t pid=0;int s=helper(fd,1,&pid),result[4]={-1,0,0,0};if(s>=0){if(ready(s,8000))full(s,result,sizeof(result),0);close(s);}CHECK("cross_process_shared_RX",s>=0&&result[0]==42,"helper=%d pid=%d value=%d mmap_errno=%d signal=%d ppid=%d",s,pid,result[0],result[1],result[2],result[3]);munmap(p,PAGE);}if(fd>=0)close(fd);
    /* PS4/PS5 JIT syscall ABI: create(flags,size,prot), alias(fd,prot); descriptors map with mmap. */
    long jit=RAW(533,0,PAGE,PROT_READ|PROT_WRITE|PROT_EXEC,0,0);CHECK("jit_create_syscall",jit>=0,"rc=%ld errno=%ld maxprot=7",jit,jit<0?-jit:0);
    if(jit>=0){long alias=RAW(534,jit,PROT_READ|PROT_WRITE,0,0,0);CHECK("jit_alias_syscall",alias>=0,"rc=%ld errno=%ld",alias,alias<0?-alias:0);void *rx=mmap(0,PAGE,5,MAP_SHARED,jit,0);void *rw=alias<0?MAP_FAILED:mmap(0,PAGE,3,MAP_SHARED,alias,0);v=-1;if(rx!=MAP_FAILED&&rw!=MAP_FAILED){memcpy(rw,code,sizeof(code));v=execute(rx);}CHECK("jit_alias_execute",v==42,"rx=%p rw=%p value=%d signal=%d errno=%d",rx,rw,v,caught,errno);if(rx!=MAP_FAILED)munmap(rx,PAGE);if(rw!=MAP_FAILED)munmap(rw,PAGE);if(alias>=0)close(alias);close(jit);}
    int (*create)(int,size_t,int,int*)=dlsym(RTLD_DEFAULT,"sceKernelJitCreateSharedMemory");
    int (*aliasfn)(int,int,int*)=dlsym(RTLD_DEFAULT,"sceKernelJitCreateAliasOfSharedMemory");
    int (*mapfn)(int,int,void**)=dlsym(RTLD_DEFAULT,"sceKernelJitMapSharedMemory");
    void *web=NULL;
    if(!create||!aliasfn||!mapfn){web=dlopen("libkernel_web.sprx",RTLD_NOW|RTLD_LOCAL);ps5log("VALUE dlopen_libkernel_web handle=%p error=%s",web,web?"none":dlerror());if(web){if(!create)create=dlsym(web,"sceKernelJitCreateSharedMemory");if(!aliasfn)aliasfn=dlsym(web,"sceKernelJitCreateAliasOfSharedMemory");if(!mapfn)mapfn=dlsym(web,"sceKernelJitMapSharedMemory");}}
    ps5log("VALUE JIT_exports create=%p alias=%p map=%p",create,aliasfn,mapfn);
    int jfd=-1,afd=-1;
    if(create){int rc=create(0,PAGE,7,&jfd);CHECK("sceKernelJitCreateSharedMemory",rc==0,"rc=%d hex=%08x fd=%d",rc,(unsigned)rc,jfd);}
    else CHECK("sceKernelJitCreateSharedMemory",0,"symbol_absent=1 rc=UNAVAILABLE");
    if(aliasfn){int rc=aliasfn(jfd,3,&afd);CHECK("sceKernelJitCreateAliasOfSharedMemory",rc==0,"rc=%d hex=%08x source_fd=%d alias_fd=%d",rc,(unsigned)rc,jfd,afd);}
    else CHECK("sceKernelJitCreateAliasOfSharedMemory",0,"symbol_absent=1 rc=UNAVAILABLE");
    if(mapfn){void *rx=0;int rc=mapfn(jfd,5,&rx);CHECK("sceKernelJitMapSharedMemory",rc==0,"rc=%d hex=%08x fd=%d address=%p",rc,(unsigned)rc,jfd,rx);if(!rc&&rx){void *rw=afd<0?MAP_FAILED:mmap(0,PAGE,3,MAP_SHARED,afd,0);v=-1;if(rw!=MAP_FAILED){memcpy(rw,code,sizeof(code));v=execute(rx);}CHECK("sceJIT_alias_execute",v==42,"rx=%p rw=%p value=%d signal=%d errno=%d",rx,rw,v,caught,rw==MAP_FAILED?errno:0);if(rw!=MAP_FAILED)munmap(rw,PAGE);munmap(rx,PAGE);}}
    else CHECK("sceKernelJitMapSharedMemory",0,"symbol_absent=1 rc=UNAVAILABLE");
    if(afd>=0)close(afd);if(jfd>=0)close(jfd);
    if(web)dlclose(web);
    return finish();}
