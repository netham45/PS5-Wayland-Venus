/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "fs.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
uint8_t *fs_readfile(const char *path, size_t *size) {
 int fd=open(path,O_RDONLY);
 if(fd<0) return NULL;
 struct stat st;
 if(fstat(fd,&st) || st.st_size<=0 || st.st_size>512*1024*1024) { close(fd); return NULL; }
 size_t len=(size_t)st.st_size, used=0;
 uint8_t *buf=malloc(len);
 if(!buf) { close(fd); return NULL; }
 while(used<len) {
  ssize_t n=read(fd,buf+used,len-used);
  if(n<0 && errno==EINTR) continue;
  if(n<=0) { free(buf); close(fd); return NULL; }
  used+=n;
 }
 close(fd);
 if(size) *size=len;
 return buf;
}
