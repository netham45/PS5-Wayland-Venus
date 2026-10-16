/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "ps5log.h"
#include <stdint.h>
#include <sys/syscall.h>
#include <unistd.h>
#include "app.h"
int main(void) {
 syscall(SYS_thr_set_name,-1,"ps5wl-launch-probe");
 if(ps5log_init("ps5wl-launch-probe",9582)) return 1;
 app_info_t info={0};
 int info_rc=sceKernelGetAppInfo(getpid(),&info);
 ps5log("own app info rc=0x%x app_id=0x%x title=%.14s",info_rc,info.app_id,info.title_id);
 for(int i=0;i<60;i++) {
  int foreground=sceSystemServiceGetAppIdOfRunningBigApp();
  ps5log("foreground probe pid=%d uid=%u own_app=0x%x running_bigapp=0x%x match=%d tick=%d",
         getpid(),getuid(),info.app_id,foreground,!info_rc && info.app_id>0 && info.app_id==(uint32_t)foreground,i);
  sleep(1);
 }
 ps5log("diagnostic complete; requesting SDL-style shell exit");
 int exit_rc=sceSystemServiceLoadExec("exit",NULL);
 ps5log("sceSystemServiceLoadExec(exit) returned rc=0x%x; shell termination is asynchronous",exit_rc);
 /* RetroArch 7d45412b src/main.cpp:722-725 waits for asynchronous shell exit.
  * Kernel _exit/SIGKILL is not a valid native-title lifecycle substitute. */
 for(;;) sleep(1);
}
