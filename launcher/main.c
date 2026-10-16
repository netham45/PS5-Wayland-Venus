/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <ps5/kernel.h>
#include "ps5log.h"
#include "hbldr.h"
#include "fs.h"

#define TITLE "PSWL00001"
#define APP "/user/app/" TITLE
#define ROOT "/data/ps5wl"
#define TARGET ROOT "/bin/ps5wl-compositor.elf"
#define ASSET(name, file) \
 __asm__(".section .rodata\n.global " #name "\n.global " #name "_end\n" \
 #name ":\n.incbin \"" file "\"\n" #name "_end:\n.previous\n"); \
 extern const unsigned char name[], name##_end[]
#ifndef UNINSTALL
ASSET(payload, "payload.elf");
ASSET(icon, "icon0.png");
static const char param[] = "{\"titleId\":\"" TITLE "\",\"applicationCategoryType\":65536,"
 "\"deeplinkUri\":\"http://127.0.0.1:9581/launch\",\"localizedParameters\":{"
 "\"defaultLanguage\":\"en-US\",\"en-US\":{\"titleName\":\"PS5 Wayland\"}}}\n";
#endif
/* API signatures and registration: Payload Manager v0.5.2
 * src/app_installer.c:33-36,51-68,136-175; metadata assets/param.json.
 * Service authid prerequisite: websrv 7748df9 src/ps5/sys.c:554-556. */
int sceAppInstUtilInitialize(void);
int sceAppInstUtilTerminate(void);
int sceAppInstUtilAppInstallAll(void *);
int sceAppInstUtilAppUnInstall(const char *);
int sceUserServiceInitialize(void *);
static volatile sig_atomic_t running = 1;
static void stop(int sig) { (void)sig; running = 0; }

#ifndef UNINSTALL
static int directory(const char *path) {
 if (!mkdir(path, 0755)) return 0;
 struct stat st;
 return errno == EEXIST && !stat(path, &st) && S_ISDIR(st.st_mode) ? 0 : -1;
}
static int save(const char *path, const void *data, size_t size) {
 char tmp[512];
 if (snprintf(tmp, sizeof(tmp), "%s.new", path) >= (int)sizeof(tmp)) return -1;
 int fd = open(tmp, O_CREAT | O_WRONLY | O_TRUNC, 0755);
 if (fd < 0) return -1;
 const unsigned char *p = data;
 size_t left = size;
 while (left) {
  ssize_t n = write(fd, p, left);
  if (n < 0 && errno == EINTR) continue;
  if (n <= 0) { close(fd); unlink(tmp); return -1; }
  p += n; left -= n;
 }
 int rc = fsync(fd);
 if (close(fd)) rc = -1;
 if (!rc) rc = rename(tmp, path);
 if (rc) unlink(tmp);
 return rc;
}

static int install(void) {
 if (directory(ROOT) || directory(ROOT "/bin") || directory(APP) || directory(APP "/sce_sys")) return -1;
 if (save(TARGET, payload, payload_end-payload) ||
     save(APP "/sce_sys/param.json", param, sizeof(param)-1) ||
     save(APP "/sce_sys/icon0.png", icon, icon_end-icon)) return -1;
 uint32_t handle;
 int (*install_dir)(const char *, const char *, void *) = NULL;
 if (!kernel_dynlib_handle(-1, "libSceAppInstUtil.sprx", &handle))
  install_dir = (void *)kernel_dynlib_resolve(-1, handle, "Wudg3Xe3heE");
 int rc = install_dir ? install_dir(TITLE, "/user/app/", NULL) : sceAppInstUtilAppInstallAll(NULL);
 ps5log("register title=%s method=%s rc=0x%x bytes=%zu", TITLE,
        install_dir ? "AppInstallTitleDir" : "AppInstallAll", rc, (size_t)(payload_end-payload));
 return rc ? -1 : 0;
}
static void reply(int fd, const char *status, const char *body) {
 char header[256];
 int n = snprintf(header, sizeof(header), "HTTP/1.1 %s\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: %zu\r\nConnection: close\r\nCache-Control: no-store\r\n\r\n", status, strlen(body));
 const char *parts[] = {header, body};
 size_t sizes[] = {(size_t)n, strlen(body)};
 for (int part=0; part<2; part++) {
  const char *p=parts[part]; size_t left=sizes[part];
  while(left) { ssize_t sent=send(fd,p,left,0); if(sent<0 && errno==EINTR) continue; if(sent<=0) return; p+=sent; left-=sent; }
 }
}
static int serve(int sock) {
 pid_t child = -1;
 static char *argv[] = {TARGET, NULL};
 static char *envp[] = {"HOME=" ROOT, "XDG_RUNTIME_DIR=/system_tmp", "WAYLAND_DISPLAY=wayland-0", NULL};
 while (running) {
  int fd = accept(sock, NULL, NULL);
  if (fd < 0) { if(errno == EINTR) continue; return -1; }
  struct timeval timeout = {.tv_sec=3};
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
  char buf[2048]; size_t used=0;
  while(used<sizeof(buf)-1) {
   ssize_t n=recv(fd,buf+used,sizeof(buf)-1-used,0);
   if(n<=0) break;
   used+=n; buf[used]=0;
   if(strstr(buf,"\r\n\r\n")) break;
  }
  buf[used]=0;
  if (!strncmp(buf, "GET /launch HTTP/1.", 18)) {
   if (child > 0 && !kill(child, 0)) {
    reply(fd,"200 OK","<h1>PS5 Wayland is already running</h1><p>Return to the PS5 Wayland foreground app.</p>");
    close(fd); continue;
   }
   ps5log("launch HTTP request; foreground launch target=%s", TARGET);
   reply(fd,"200 OK","<h1>Starting PS5 Wayland</h1><p>The foreground app will replace this browser. Close any running game first. Logs: port 9580.</p>");
   close(fd);
   child=hbldr_launch(ROOT,TARGET,STDERR_FILENO,argv,envp);
   ps5log("foreground launch result pid=%d errno=%d", (int)child, errno);
  } else {
   reply(fd,"404 Not Found","<h1>PS5 Wayland</h1><a href='/launch'>Launch</a>"); close(fd);
  }
 }
 return 0;
}
#endif

int main(void) {
 syscall(SYS_thr_set_name, -1,
#ifdef UNINSTALL
 "ps5wl-uninstall.elf"
#else
 "ps5wl-install.elf"
#endif
 );
 if (ps5log_init("ps5wl-launcher", 9580)) return 1;
 ps5log("started pid=%d uid=%u firmware=0x%08x mode=%s", getpid(), getuid(), kernel_get_fw_version(),
#ifdef UNINSTALL
 "uninstall"
#else
 "install/server"
#endif
 );
 signal(SIGPIPE,SIG_IGN);
 uint64_t old_authid=kernel_get_ucred_authid(-1);
 int credential_rc=kernel_set_ucred_authid(-1,0x4801000000000013ULL);
 ps5log("launcher authid previous=0x%llx set_rc=%d effective_uid=%u",
        (unsigned long long)old_authid,credential_rc,geteuid());
 if(credential_rc) { ps5log_flush_wait(30000); return 1; }
 signal(SIGINT,stop); signal(SIGTERM,stop);
 int rc=sceAppInstUtilInitialize();
 ps5log("sceAppInstUtilInitialize rc=0x%x",rc);
 if(rc) { ps5log_flush_wait(30000); return 1; }
#ifdef UNINSTALL
 rc=sceAppInstUtilAppUnInstall(TITLE);
 ps5log("unregister title=%s rc=0x%x",TITLE,rc);
 {
  const char *files[]={APP "/sce_sys/icon0.png", APP "/sce_sys/param.json", TARGET, TARGET ".new"};
  for(size_t i=0;i<sizeof(files)/sizeof(files[0]);i++) if(unlink(files[i]) && errno!=ENOENT) { ps5log("unlink %s errno=%d",files[i],errno); rc=-1; }
  const char *dirs[]={APP "/sce_sys",APP,ROOT "/bin",ROOT};
  for(size_t i=0;i<sizeof(dirs)/sizeof(dirs[0]);i++) {
   int result=rmdir(dirs[i]), error=errno;
   if(result && error!=ENOENT) {
    ps5log("rmdir %s errno=%d (unrelated files retained)",dirs[i],error);
    if(error!=ENOTEMPTY) rc=-1;
   }
  }
  if(hbldr_remove()) rc=-1;
 }
 sceAppInstUtilTerminate();
 ps5log("uninstall complete rc=%d",rc);
 ps5log_flush_wait(30000);
 return rc ? 1 : 0;
#else
 int sock=socket(AF_INET,SOCK_STREAM,0);
 struct sockaddr_in addr={.sin_family=AF_INET,.sin_port=htons(9581),.sin_addr.s_addr=htonl(INADDR_LOOPBACK)};
 if(sock<0 || bind(sock,(void *)&addr,sizeof(addr)) || listen(sock,8)) {
  ps5log("loopback 9581 unavailable errno=%d; stop existing receipt-owned installer before updating",errno);
  if(sock>=0) close(sock);
  sceAppInstUtilTerminate(); ps5log_flush_wait(30000); return 1;
 }
 rc=install();
 ps5log("install result=%d errno=%d stable_target=%s",rc,errno,TARGET);
 sceAppInstUtilTerminate();
 if(!rc) {
  int priority=256;
  rc=sceUserServiceInitialize(&priority);
  ps5log("sceUserServiceInitialize rc=0x%x; ready http=127.0.0.1:9581 logs=9580",rc);
  if(!rc) rc=serve(sock);
 }
 close(sock); ps5log_flush_wait(30000); return rc ? 1 : 0;
#endif
}
