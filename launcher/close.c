/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "app.h"
#include "ps5log.h"
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#include <unistd.h>
int sceUserServiceInitialize(void *param);

/* Shipping sequence: mihawk-99/PS5_Vulkan f410e67
 * payload/ps5vkctl/main.c:195-282. Only our exact title may be affected. */
static int close_owned_title(void) {
 uint32_t app_id=sceLncUtilGetAppIdOfRunningBigApp();
 char title[32]={0};
 int title_rc=sceLncUtilGetAppTitleId(app_id,title);
 ps5log("running app_id=0x%x title_rc=0x%x title=%s",app_id,title_rc,title);
 if(!app_id || app_id==UINT32_MAX || title_rc || strcmp(title,"PSWL00002")) {
  ps5log("refusing close: running application is not our PSWL00002 title");
  return -1;
 }
 int suspend_rc=sceLncUtilSuspendApp(app_id);
 ps5log("sceLncUtilSuspendApp(0x%x) rc=0x%x",app_id,suspend_rc);
 sleep(2);
 int mib[]={CTL_KERN,KERN_PROC,KERN_PROC_PROC,0};
 size_t size=0;
 if(sysctl(mib,4,NULL,&size,NULL,0)) return -1;
 unsigned char *data=malloc(size);
 if(!data) return -1;
 if(sysctl(mib,4,data,&size,NULL,0)) { free(data); return -1; }
 for(size_t offset=0;offset+sizeof(struct kinfo_proc)<=size;) {
  struct kinfo_proc *process=(void *)(data+offset);
  if(process->ki_structsize<(int)sizeof(*process) || (size_t)process->ki_structsize>size-offset) break;
  offset+=(size_t)process->ki_structsize;
  if(process->ki_pid==getpid()) continue;
  app_info_t info={0};
  if(sceKernelGetAppInfo(process->ki_pid,&info) || info.app_id!=app_id || strncmp(info.title_id,"PSWL00002",14)) continue;
  int result=kill(process->ki_pid,SIGKILL), error=result<0 ? errno : 0;
  ps5log("owned title SIGKILL pid=%d result=%d errno=%d",process->ki_pid,result,error);
 }
 free(data);
 uint32_t result=sceLncUtilKillApp(app_id);
 ps5log("sceLncUtilKillApp(0x%x) result=0x%x",app_id,result);
 for(int i=0;i<50;i++) {
  uint32_t current=sceLncUtilGetAppIdOfRunningBigApp();
  if(!current || current==UINT32_MAX) {
   ps5log("title closed; running app_id=0x%x",current);
   return 0;
  }
  usleep(100000);
 }
 ps5log("title still registered after five-second wait");
 return -1;
}
int main(void) {
 if(ps5log_init("ps5wl-close",9585)) _exit(1);
 ps5log("close helper started pid=%d euid=%u",getpid(),geteuid());
 /* Same user-service prerequisite as ps5vkctl/main.c:502-503. */
 int rc=sceUserServiceInitialize(NULL);
 ps5log("sceUserServiceInitialize rc=0x%x",rc);
 if(!rc) rc=close_owned_title();
 ps5log("close helper complete rc=%d",rc);
 ps5log_flush_wait(10000);
 _exit(rc ? 1 : 0);
}
