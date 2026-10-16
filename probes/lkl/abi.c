#include "cap.h"
#include <dlfcn.h>
#include <setjmp.h>
#include <spawn.h>

static sigjmp_buf jump;
static volatile int caught;
static void trap(int s) { caught = s; siglongjmp(jump, 1); }

static void spawn_api(void) {
    int (*spawnfn)(pid_t*,const char*,const posix_spawn_file_actions_t*,const posix_spawnattr_t*,char *const[],char *const[]) = dlsym(RTLD_DEFAULT,"posix_spawn");
    int (*init)(posix_spawn_file_actions_t*) = dlsym(RTLD_DEFAULT,"posix_spawn_file_actions_init");
    int (*addclose)(posix_spawn_file_actions_t*,int) = dlsym(RTLD_DEFAULT,"posix_spawn_file_actions_addclose");
    int (*destroy)(posix_spawn_file_actions_t*) = dlsym(RTLD_DEFAULT,"posix_spawn_file_actions_destroy");
    posix_spawn_file_actions_t actions = NULL;
    int initialized = 0;
    if (init && addclose && destroy) {
        int rc = init(&actions);
        initialized = rc == 0;
        CHECK("spawn_file_actions_init", rc == 0, "rc=%d object=%p", rc, actions);
        if (initialized) {
            rc = addclose(&actions, 12345);
            CHECK("spawn_file_actions_addclose", rc == 0, "rc=%d", rc);
        }
    } else ps5log("UNTESTED spawn_file_actions_behavior symbols_missing=1");
    if (spawnfn) {
        pid_t pid = -1;
        char *args[] = {"ps5wl-lklcap-helper.elf",NULL}, *environment[] = {NULL};
        int rc = spawnfn(&pid,"/data/pldmgr/payloads/ps5wl-lklcap-helper/ps5wl-lklcap-helper.elf",
            NULL,NULL,args,environment);
        CHECK("posix_spawn_unsigned_runner", rc == 0, "rc=%d pid=%d", rc, pid);
        if (!rc && pid > 0) {
            owned_child = pid;
            int status;
            if (reap(pid,&status,5000)) retire(pid);
            owned_child = 0;
        }
    }
    if (initialized) {
        int rc = destroy(&actions);
        CHECK("spawn_file_actions_destroy", rc == 0, "rc=%d", rc);
    }
}

int main(void) {
    begin("lklcap-abi",9612);
    signal(SIGSYS,trap);
    const char *names[] = {
        "memfd_create","posix_spawn","posix_spawnp",
        "posix_spawn_file_actions_init","posix_spawn_file_actions_adddup2",
        "posix_spawn_file_actions_addopen","posix_spawn_file_actions_addclose",
        "posix_spawn_file_actions_destroy","copy_file_range","pdfork",
        "cap_enter","cap_getmode","cap_rights_limit","cap_rights_get",
        "cap_ioctls_limit","cap_ioctls_get","cap_fcntls_limit","cap_fcntls_get","eventfd","eventfd2",
        "timerfd_create","timerfd_settime","getrandom","getentropy","pipe2","accept4"
    };
    int (*lookup)(int,const char*,void**) = dlsym(RTLD_DEFAULT,"sceKernelDlsym");
    int handle=1;
    void *positive=NULL;
    int control_rc=lookup?lookup(handle,"getpid",&positive):-1;
    if(control_rc||!positive){handle=0x2001;control_rc=lookup?lookup(handle,"getpid",&positive):-1;}
    CHECK("ABI_lookup_positive_control",control_rc==0&&positive!=NULL,
        "symbol=getpid handle=%x rc=%d address=%p",handle,control_rc,positive);
    for (unsigned i=0;i<sizeof(names)/sizeof(*names);i++) {
        void *p=dlsym(RTLD_DEFAULT,names[i]), *native=NULL;
        int rc=lookup?lookup(handle,names[i],&native):-1;
        Dl_info origin={0};
        if(p)dladdr(p,&origin);
        if(control_rc||!positive){ps5log("UNTESTED ABI_libkernel_symbol name=%s lookup_control_failed=1 default=%p default_library=%s",
            names[i],p,origin.dli_fname?origin.dli_fname:"unknown");continue;}
        CHECK("ABI_libkernel_symbol",native!=NULL,
            "name=%s native=%p sceKernelDlsym_rc=%d hex=%08x default=%p default_library=%s",
            names[i],native,rc,(unsigned)rc,p,origin.dli_fname?origin.dli_fname:"unknown");
    }
    spawn_api();
    /* SDK's BSD-11 numbers, invalid args: no process creation or sandbox mutation.
     * Dispatch only: non-ENOSYS does NOT establish BSD semantics on the PS5 fork. */
    struct {const char *name;int nr;long a,b,c,d,e;} calls[] = {
        {"pdfork",SYS_pdfork,0,-1,0,0,0},
        {"cap_getmode",SYS_cap_getmode,0,0,0,0,0},
        {"cap_rights_get",SYS___cap_rights_get,0,-1,0,0,0},
        {"pipe2",SYS_pipe2,0,-1,0,0,0},
        {"accept4",SYS_accept4,-1,0,0,0,0}
    };
    for(unsigned i=0;i<sizeof(calls)/sizeof(*calls);i++) {
        caught=0;
        long rc=-ENOSYS;
        if(sigsetjmp(jump,1)==0)
            rc=RAW(calls[i].nr,calls[i].a,calls[i].b,calls[i].c,calls[i].d,calls[i].e);
        CHECK("ABI_raw_dispatch",!caught&&rc!=-ENOSYS,
            "name=%s SDK_BSD_number=%d rc=%ld errno=%ld signal=%d invalid_arguments=1 semantics_unproven=1",
            calls[i].name,calls[i].nr,rc,rc<0?-rc:0,caught);
    }
    ps5log("UNTESTED ABI_raw memfd_create,posix_spawn,file_actions,copy_file_range,eventfd,timerfd,getrandom: no verified PS5 syscall number; modern BSD/Linux numbers overlap proprietary PS5 ABI; absence of symbol is not ENOSYS evidence");
    ps5log("UNTESTED ABI_raw cap_enter/cap_*_limit: sandbox mutation excluded; FreeBSD cap_rights_limit 533 aliases PS5 JIT create");
    return finish();
}
