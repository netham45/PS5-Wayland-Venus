/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "ps5log.h"
#include <stdint.h>
#include <sys/syscall.h>
#include <unistd.h>
/* websrv 7748df9 src/ps5/hbldr.c:78,413 queries the foreground BigApp. */
int sceSystemServiceGetAppIdOfRunningBigApp(void);
int main(void) {
 syscall(SYS_thr_set_name,-1,"ps5wl-launch-probe");
 if(ps5log_init("ps5wl-launch-probe",9582)) return 1;
 for(int i=0;i<60;i++) {
  int foreground=sceSystemServiceGetAppIdOfRunningBigApp();
  ps5log("foreground probe pid=%d uid=%u running_bigapp=0x%x tick=%d",getpid(),getuid(),foreground,i);
  sleep(1);
 }
 _exit(0);
}
