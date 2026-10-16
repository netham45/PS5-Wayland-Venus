/* Copyright (C) 2024 John Törnblom

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING. If not, see
<http://www.gnu.org/licenses/>.  */

/* Ported from websrv 7748df9 src/ps5/hbldr.c:45-453.
 * Its signed-launch / ptrace sequence is retained; project-specific changes
 * are documented in README.md. No unsigned-title ABI is invented here. */
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>

#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#include <sys/wait.h>

#include <ps5/kernel.h>

#include "fs.h"
#include "elfldr.h"
#include "hbldr.h"
#include "pt.h"
#include "ps5log.h"
#include <errno.h>
#define puts(s) ps5log("%s", (s))
#define perror(s) ps5log("%s: errno=%d (%s)", (s), errno, strerror(errno))


#define PSNOW_EBOOT "/system_ex/app/NPXS40106/eboot.bin"
#define FAKE_PATH "/system_ex/app/PSWL00002"

#define IOVEC_ENTRY(x) {(x) ? (x) : 0, \
			(x) ? strlen(x)+1 : 0}
#define IOVEC_SIZE(x) (sizeof(x) / sizeof(struct iovec))


static const char param_json[] = "{\n"
  "  \"applicationCategoryType\": 0,\n"
  "  \"attribute\": 1,\n"
  "  \"attribute2\": 0,\n"
  "  \"attribute3\": 4,\n"
  "  \"titleId\": \"PSWL00002\",\n"
  "  \"contentId\": \"IV9999-PSWL00002_00-PS5WAYLAND000000\",\n"
  "  \"localizedParameters\": {\n"
  "    \"defaultLanguage\": \"en-US\",\n"
  "    \"en-US\": {\n"
  "      \"titleName\": \"PS5 Wayland\"\n"
  "    }\n"
  "  }\n"
  "}\n";


typedef struct app_launch_ctx {
  uint32_t structsize;
  uint32_t user_id;
  uint32_t app_opt;
  uint64_t crash_report;
  uint32_t check_flag;
} app_launch_ctx_t;


int sceUserServiceInitialize(void*);
int sceUserServiceGetForegroundUser(uint32_t *user_id);
int sceUserServiceTerminate(void);

int sceSystemServiceGetAppIdOfRunningBigApp(void);
int sceSystemServiceLaunchApp(const char* title_id, char** argv,
			      app_launch_ctx_t* ctx);



static int
remount_system_ex(int flags) {
  struct iovec iov[] = {
    IOVEC_ENTRY("from"),      IOVEC_ENTRY("/dev/ssd0.system_ex"),
    IOVEC_ENTRY("fspath"),    IOVEC_ENTRY("/system_ex"),
    IOVEC_ENTRY("fstype"),    IOVEC_ENTRY("exfatfs"),
    IOVEC_ENTRY("large"),     IOVEC_ENTRY("yes"),
    IOVEC_ENTRY("timezone"),  IOVEC_ENTRY("static"),
    IOVEC_ENTRY("async"),     IOVEC_ENTRY(NULL),
    IOVEC_ENTRY("ignoreacl"), IOVEC_ENTRY(NULL),
    /* FreeBSD releng/11.0 sys/kern/vfs_mount.c:192,225,635-640,944:
     * explicitly replace inherited "ro" when upgrading an nmount mount. */
    IOVEC_ENTRY((flags & MNT_RDONLY) ? "ro" : "noro"), IOVEC_ENTRY(NULL),
  };

  int result=nmount(iov, IOVEC_SIZE(iov), MNT_UPDATE | flags);
  ps5log("nmount system_ex flags=0x%x result=%d errno=%d", flags, result, errno);
  struct statfs current;
  const char *paths[]={"/system_ex",FAKE_PATH};
  for(size_t i=0;i<2;i++) {
    if(!statfs(paths[i],&current))
      ps5log("statfs %s flags=0x%lx type=%s mount=%s from=%s",paths[i],
             (unsigned long)current.f_flags,current.f_fstypename,current.f_mntonname,current.f_mntfromname);
  }
  if(result) return -1;

  return 0;
}



static int
fakeapp_create_if_missing(void) {
  struct stat info;
  uint8_t* buf;
  size_t size;
  int fd;

  if(stat(FAKE_PATH, &info)) {
    if(mkdir(FAKE_PATH, 0755)) {
      return -1;
    }
  }

  if(stat(FAKE_PATH "/sce_sys", &info)) {
    if(mkdir(FAKE_PATH "/sce_sys", 0755)) {
      return -1;
    }
  }

  if(stat(FAKE_PATH "/sce_sys/param.json", &info)) {
    if((fd=open(FAKE_PATH "/sce_sys/param.json", O_CREAT|O_WRONLY, 0644)) < 0) {
      return -1;
    }
    if(write(fd, param_json, sizeof(param_json)-1) != sizeof(param_json)-1) {
      close(fd);
      return -1;
    }
    close(fd);
  }

  if(stat(FAKE_PATH "/eboot.bin", &info)) {
    if(!(buf=fs_readfile(PSNOW_EBOOT, &size))) {
      return -1;
    }
    if((fd=open(FAKE_PATH "/eboot.bin", O_CREAT|O_WRONLY, 0755)) < 0) {
      free(buf);
      return -1;
    }
    if(write(fd, buf, size) != size) {
      free(buf);
      close(fd);
      return -1;
    }
    free(buf);
    close(fd);
  }
  return 0;
}


/**
 * Get the pid of a process with the given name.
 **/
static pid_t
find_pid(const char* name) {
  int mib[4] = {1, 14, 8, 0};
  pid_t mypid = getpid();
  pid_t pid = -1;
  size_t buf_size;
  uint8_t *buf;

  if(sysctl(mib, 4, 0, &buf_size, 0, 0)) {
    perror("sysctl");
    return -1;
  }

  if(!(buf=malloc(buf_size))) {
    perror("malloc");
    return -1;
  }

  if(sysctl(mib, 4, buf, &buf_size, 0, 0)) {
    perror("sysctl");
    free(buf);
    return -1;
  }

  for(uint8_t *ptr=buf; ptr<(buf+buf_size);) {
    int ki_structsize = *(int*)ptr;
    if(ki_structsize < 480 || (size_t)ki_structsize > (size_t)(buf + buf_size - ptr)) {
      break;
    }
    pid_t ki_pid = *(pid_t*)&ptr[72];
    char *ki_tdname = (char*)&ptr[447];

    ptr += ki_structsize;
    if(!strcmp(name, ki_tdname) && mypid != ki_pid) {
      pid = ki_pid;
    }
  }

  free(buf);

  return pid;
}


/**
 *
 **/
static void*
bigapp_launch_thread(void* arg) {
  char** argv = (char**)arg;
  app_launch_ctx_t ctx = {0};

  int user_rc = sceUserServiceGetForegroundUser(&ctx.user_id);
  if(user_rc) {
    ps5log("sceUserServiceGetForegroundUser rc=0x%x", user_rc);
    return 0;
  }

  int rc = sceSystemServiceLaunchApp("PSWL00002", argv, &ctx);
  ps5log("sceSystemServiceLaunchApp title=PSWL00002 user=%u rc=0x%x",
         ctx.user_id, rc);

  return 0;
}


/**
 *
 **/
static pid_t
bigapp_launch(char** argv) {
  struct kevent evt = {0};
  pid_t parent = -1;
  pid_t child = -1;
  pthread_t trd;
  int kq = -1;

  if((parent=find_pid("SceSysCore.elf")) < 0) {
    perror("findpid");
    return -1;
  }

  if((kq=kqueue()) < 0) {
    perror("kqueue");
    return -1;
  }

  EV_SET(&evt, parent, EVFILT_PROC, EV_ADD | EV_ENABLE | EV_CLEAR,
	 NOTE_FORK | NOTE_EXEC | NOTE_TRACK, 0, 0);
  if(kevent(kq, &evt, 1, 0, 0, 0) < 0) {
    perror("kevent");
    close(kq);
    return -1;
  }

  int rc = pthread_create(&trd, 0, &bigapp_launch_thread, (void*)argv);
  if(rc) {
    ps5log("pthread_create rc=%d", rc);
    close(kq);
    return -1;
  }
  pthread_detach(trd);

  struct timespec timeout = {.tv_sec = 15};
  if(kevent(kq, 0, 0, &evt, 1, &timeout) != 1) {
    perror("kevent waiting for BigApp child");
    close(kq);
    return -1;
  }

  close(kq);

  if(!(evt.fflags & NOTE_CHILD)) {
    return -1;
  }

  child = evt.ident;

  if(pt_attach(child)) {
    perror("pt_attach");
    return -1;
  }

  if(pt_follow_exec(child) < 0) {
    perror("pt_follow_exec");
    pt_detach(child, SIGKILL);
    return -1;
  }

  if(pt_continue(child, SIGCONT) < 0) {
    perror("pt_continue");
    pt_detach(child, SIGKILL);
    return -1;
  }

  if(pt_await_exec(child)) {
    perror("pt_await_exec");
    pt_detach(child, SIGKILL);
    return -1;
  }

  return child;
}


/**
 *
 **/
static int
bigapp_set_argv0(pid_t pid, const char* argv0) {
  intptr_t pos = pt_getargv(pid);
  intptr_t buf = 0;

  // allocate memory
  if((buf=pt_mmap(pid, 0, PAGE_SIZE, PROT_WRITE | PROT_READ,
		  MAP_ANONYMOUS | MAP_PRIVATE,
		  -1, 0)) == -1) {
    pt_perror(pid, "pt_mmap");
    return -1;
  }

  // copy string
  if(pt_copyin(pid, argv0, buf, strlen(argv0)+1)) {
    perror("pt_copyin");
    pt_munmap(pid, buf, PAGE_SIZE);
    return -1;
  }

  // copy pointer to string
  if(pt_setlong(pid, pos, buf)) {
    perror("pt_setlong");
    pt_munmap(pid, buf, PAGE_SIZE);
    return -1;
  }

  return 0;
}


/**
 *
 **/
static pid_t
bigapp_replace(pid_t pid, uint8_t* elf, const char* progname, int stdio,
	       const char* cwd, char** envp) {
  uint8_t int3instr = 0xcc;
  intptr_t brkpoint;
  uint8_t orginstr;

  // Let the kernel assign process parameters accessed via sceKernelGetProcParam()
  if(pt_syscall(pid, 599)) {
    puts("sys_dynlib_process_needed_and_relocate failed");
    //return -1;
  }

  // Allow libc to allocate arbitrary amount of memory.
  elfldr_set_heap_size(pid, -1);

  if(!(brkpoint=kernel_dynlib_entry_addr(pid, 0))) {
    puts("kernel_dynlib_entry_addr failed");
    return -1;
  }
  brkpoint += 58;// offset to invocation of main()

  if(kernel_mprotect(pid, brkpoint, PAGE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC)) {
    puts("kernel_mprotect failed");
    pt_detach(pid, SIGKILL);
    return -1;
  }

  if(pt_copyout(pid, brkpoint, &orginstr, sizeof(orginstr))) {
    perror("pt_copyout");
    return -1;
  }
  if(pt_copyin(pid, &int3instr, brkpoint, sizeof(int3instr))) {
    perror("pt_copyin");
    return -1;
  }

  // Continue execution until we hit the breakpoint, then remove it.
  if(pt_continue(pid, SIGCONT)) {
    perror("pt_continue");
    return -1;
  }
  if(waitpid(pid, 0, 0) == -1) {
    perror("waitpid");
    return -1;
  }
  if(pt_copyin(pid, &orginstr, brkpoint, sizeof(orginstr))) {
    perror("pt_copyin");
    return -1;
  }

  if(bigapp_set_argv0(pid, progname) ||
     elfldr_raise_privileges(pid) ||
     elfldr_set_procname(pid, basename(progname)) ||
     elfldr_set_environ(pid, envp) ||
     elfldr_set_cwd(pid, cwd) ||
     elfldr_set_stdio(pid, stdio)) {
    ps5log("BigApp process setup failed pid=%d errno=%d", pid, errno);
    return -1;
  }

  // invoke sys_set_butget(0). This allows bigapp homebrew to operate on
  // relative paths using open, mkdir, etc. TODO: On older firmwares, this
  // causes a kernel panic when the bigapp is restarted. Figure out what
  // firmwares this issue affect, and how to address it.
  if(kernel_get_fw_version() > 0x10000000) {
    pt_syscall(pid, 0x23b, 0);
  }

  // Execute the ELF
  if(elfldr_exec(pid, elf)) {
    return -1;
  }

  return pid;
}


pid_t
hbldr_launch(const char*cwd, const char* path, int stdio, char** argv,
	     char** envp) {
  char buf[PATH_MAX];
  uint8_t* elf;
  int app_id;
  pid_t pid;

  if((app_id=sceSystemServiceGetAppIdOfRunningBigApp()) > 0) {
    ps5log("Refusing launch: existing BigApp id=0x%x; close it manually first", app_id);
    errno = EBUSY;
    return -1;
  }

  struct statfs mount_info;
  if(statfs("/system_ex", &mount_info)) return -1;
  int was_readonly = (mount_info.f_flags & MNT_RDONLY) != 0;
  if(was_readonly && remount_system_ex(0)) {
    perror("remount_system_ex writable");
    return -1;
  }
  int created = fakeapp_create_if_missing();
  if(was_readonly && remount_system_ex(MNT_RDONLY)) {
    perror("restoring system_ex readonly");
    return -1;
  }
  if(created) {
    perror("fakeapp_create_if_missing");
    return -1;
  }


  if(path[0] == '/') {
    strncpy(buf, path, sizeof(buf));
  } else {
    snprintf(buf, sizeof(buf), "%s/%s", cwd, path);
  }

  if(!(elf=fs_readfile(buf, 0))) {
    return -1;
  }

  if((pid=bigapp_launch(argv)) < 0) {
    free(elf);
    return -1;
  }

  if(bigapp_replace(pid, elf, path, stdio, cwd, envp) < 0) {
    pt_detach(pid, SIGKILL);
    pid = -1;
  }

  free(elf);

  return pid;
}

int hbldr_remove(void) {
  struct stat st;
  if(stat(FAKE_PATH, &st)) return errno == ENOENT ? 0 : -1;
  struct statfs mount_info;
  if(statfs("/system_ex", &mount_info)) return -1;
  int was_readonly = (mount_info.f_flags & MNT_RDONLY) != 0;
  ps5log("remove BigApp mount flags=0x%lx readonly=%d", (unsigned long)mount_info.f_flags, was_readonly);
  if(was_readonly && remount_system_ex(0)) {
    perror("remove BigApp remount writable");
    return -1;
  }
  int rc = 0;
  const char *files[] = {FAKE_PATH "/eboot.bin", FAKE_PATH "/sce_sys/param.json"};
  for(size_t i=0; i<sizeof(files)/sizeof(files[0]); i++) {
    int result = unlink(files[i]);
    ps5log("remove %s result=%d errno=%d", files[i], result, errno);
    if(result && errno != ENOENT) rc = -1;
  }
  if(rmdir(FAKE_PATH "/sce_sys") && errno != ENOENT) { perror("remove BigApp sce_sys"); rc = -1; }
  if(rmdir(FAKE_PATH) && errno != ENOENT) { perror("remove BigApp root"); rc = -1; }
  if(was_readonly && remount_system_ex(MNT_RDONLY)) { perror("remove BigApp restore readonly"); rc = -1; }
  ps5log("remove BigApp directory rc=%d errno=%d", rc, errno);
  return rc;
}
