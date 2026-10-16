/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <errno.h>
#include <stdint.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>
#include "ps5log.h"
int main(void) {
 if(ps5log_init("ps5wl-launch-state",9584)) _exit(1);
 ps5log("state pid=%d uid=%u euid=%u",getpid(),getuid(),geteuid());
 const char *paths[]={
  "/user/app/PSWL00001/sce_sys/param.json",
  "/user/app/PSWL00001/sce_sys/icon0.png",
  "/user/appmeta/PSWL00001/param.json",
  "/data/ps5wl", "/data/ps5wl/bin/ps5wl-compositor.elf",
  "/system_ex/app/PSWL00002", "/system_ex/app/PSWL00002/eboot.bin",
  "/system_ex/app/PSWL00002/sce_sys/param.json"
 };
 for(size_t i=0;i<sizeof(paths)/sizeof(paths[0]);i++) {
  struct stat st;
  int rc=stat(paths[i],&st), error=errno;
  if(!rc) ps5log("stat %s exists size=%lld mode=%o",paths[i],(long long)st.st_size,st.st_mode);
  else ps5log("stat %s rc=%d errno=%d",paths[i],rc,error);
 }
 struct statfs fs;
 if(!statfs("/system_ex",&fs))
  ps5log("system_ex flags=0x%lx type=%s from=%s",(unsigned long)fs.f_flags,fs.f_fstypename,fs.f_mntfromname);
 else ps5log("statfs system_ex errno=%d",errno);
 ps5log("state complete");
 ps5log_flush_wait(10000);
 _exit(0);
}
