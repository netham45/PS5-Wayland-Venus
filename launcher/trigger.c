/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "ps5log.h"
int main(void) {
 if(ps5log_init("ps5wl-launch-test",9583)) return 1;
 int fd=socket(AF_INET,SOCK_STREAM,0);
 struct sockaddr_in addr={.sin_family=AF_INET,.sin_port=htons(9581),.sin_addr.s_addr=htonl(INADDR_LOOPBACK)};
 if(fd<0 || connect(fd,(void *)&addr,sizeof(addr))) {
  ps5log("connect launcher errno=%d",errno);
  if(fd>=0) close(fd);
  ps5log_flush_wait(30000); return 1;
 }
 const char request[]="GET /launch HTTP/1.1\r\nHost: 127.0.0.1:9581\r\nConnection: close\r\n\r\n";
 size_t used=0;
 while(used<sizeof(request)-1) {
  ssize_t n=send(fd,request+used,sizeof(request)-1-used,0);
  if(n<0 && errno==EINTR) continue;
  if(n<=0) break;
  used+=n;
 }
 char response[2048]; ssize_t n=recv(fd,response,sizeof(response)-1,0);
 if(n>0) { response[n]=0; ps5log("HTTP response: %s",response); }
 else ps5log("read response n=%zd errno=%d",n,errno);
 close(fd); ps5log_flush_wait(30000); return n>0 ? 0 : 1;
}
