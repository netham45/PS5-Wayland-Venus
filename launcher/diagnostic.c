/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <ps5/kernel.h>
#include "hbldr.h"
#include "ps5log.h"

/* Same embedded-asset convention as main.c; no installed asset is replaced. */
__asm__(".section .rodata\n.global diagnostic_payload\n.global diagnostic_payload_end\n"
        "diagnostic_payload:\n.incbin \"diagnostic.elf\"\n"
        "diagnostic_payload_end:\n.previous\n");
extern const unsigned char diagnostic_payload[], diagnostic_payload_end[];
int sceUserServiceInitialize(void *param);

static int stage(char *path) {
 int fd=mkstemp(path);
 if(fd<0) return -1;
 const unsigned char *data=diagnostic_payload;
 size_t left=(size_t)(diagnostic_payload_end-diagnostic_payload);
 while(left) {
  ssize_t count=write(fd,data,left);
  if(count<0 && errno==EINTR) continue;
  if(count<=0) { int error=errno; close(fd); unlink(path); errno=error; return -1; }
  data+=count;
  left-=(size_t)count;
 }
 if(close(fd)) { int error=errno; unlink(path); errno=error; return -1; }
 return 0;
}

int main(void) {
 syscall(SYS_thr_set_name,-1,"ps5wl-launch-diag");
 if(ps5log_init("ps5wl-launch-diag",9586)) _exit(1);
 signal(SIGPIPE,SIG_IGN);
 int result=1;
 pid_t child=-1;
 char path[]="/data/ps5wl/ps5wl-diagnostic-XXXXXX";
 /* Same service authorization as main.c, cited to websrv 7748df9
  * src/ps5/sys.c:554-556. Changes only this background driver's credentials. */
 int auth_rc=kernel_set_ucred_authid(-1,0x4801000000000013ULL);
 ps5log("isolated driver pid=%d auth_rc=%d bytes=%zu",getpid(),auth_rc,
        (size_t)(diagnostic_payload_end-diagnostic_payload));
 if(auth_rc) goto done;
 int user_rc=sceUserServiceInitialize(NULL);
 ps5log("sceUserServiceInitialize rc=0x%x",user_rc);
 if(user_rc) goto done;
 if(stage(path)) { ps5log("diagnostic staging failed errno=%d",errno); goto done; }
 char *argv[]={path,NULL};
 char *envp[]={"HOME=/data/ps5wl","XDG_RUNTIME_DIR=/system_tmp","WAYLAND_DISPLAY=wayland-0",NULL};
 /* Existing foreground-only loader refuses any already-running BigApp.
  * It copies the ELF before returning; no fork/GPU/budget API is added here. */
 child=hbldr_launch("/data/ps5wl",path,STDERR_FILENO,argv,envp);
 int launch_error=errno;
 int unlink_rc=unlink(path), unlink_error=unlink_rc ? errno : 0;
 ps5log("diagnostic launch child=%d errno=%d staging_unlink=%d errno=%d",
        child,child<0 ? launch_error : 0,unlink_rc,unlink_error);
 if(child<0 || unlink_rc) goto done;
 /* SceSysCore, not this driver, owns the signed child: waitpid is not valid.
  * Do not signal or terminate an application on timeout; coordinator owns it. */
 for(int tick=0;tick<750;tick++) {
  if(kill(child,0)<0) {
   if(errno==ESRCH) { result=0; ps5log("diagnostic child=%d disappeared",child); }
   else ps5log("diagnostic child status failed errno=%d",errno);
   goto done;
  }
  usleep(100000);
 }
 ps5log("diagnostic child=%d still present after75 seconds; coordinator cleanup required",child);
done:
 ps5log("diagnostic result=%d child=%d",result,child);
 ps5log_flush_wait(3000);
 /* This is a plain background driver, never the native BigApp body. */
 _exit(result);
}
