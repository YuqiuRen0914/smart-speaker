#include "device_api/http/standby_listener.h"
#include <lwip/sockets.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#include "sdkconfig.h"
#ifndef CONFIG_LUNAREDU_DEVICE_API_HTTP_SERVER_PORT
#define CONFIG_LUNAREDU_DEVICE_API_HTTP_SERVER_PORT 80
#endif
namespace lunaredu::lunaros::web_internal {
namespace {
int listener=-1, client=-1;
uint32_t accepted_at=0;
char request[256];
size_t received=0, sent=0;
const char *response=nullptr;
bool wake=false;
// No request path, Host header, credential or private state is reflected.
// Reload preserves the browser's URL and lets normal HTTP enforce access.
constexpr char kNavigation[]="HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nX-Lunar-Web: standby\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width'><title>连接设备</title><p>正在连接设备…</p><script>async function ready(){try{const r=await fetch(location.href,{cache:'no-store'});if(r.headers.get('X-Lunar-Web')!=='standby'){location.reload();return}}catch{}setTimeout(ready,500)}setTimeout(ready,500)</script>";
constexpr char kRetry[]="HTTP/1.1 503 Service Unavailable\r\nRetry-After: 1\r\nCache-Control: no-store\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
void close_client(){if(client>=0)lwip_close(client);client=-1;received=sent=0;response=nullptr;wake=false;}
bool nonblocking(int fd){return fcntl(fd,F_SETFL,fcntl(fd,F_GETFL,0)|O_NONBLOCK)==0;}
}
bool standby_listener_start(std::string &error){
  error.clear();if(listener>=0)return true;
  int fd=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
  if(fd<0){error="web_listener_socket_failed";return false;}
  int reuse=1;setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));
  sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_ANY);
  address.sin_port=htons(CONFIG_LUNAREDU_DEVICE_API_HTTP_SERVER_PORT);
  if(!nonblocking(fd)||bind(fd,reinterpret_cast<sockaddr *>(&address),sizeof(address))!=0||listen(fd,1)!=0){lwip_close(fd);error="web_listener_bind_failed";return false;}
  listener=fd;return true;
}
void standby_listener_stop(){close_client();if(listener>=0)lwip_close(listener);listener=-1;}
bool standby_listener_poll(uint32_t now){
  if(listener<0)return false;
  if(client<0){
    client=accept(listener,nullptr,nullptr);if(client<0)return false;
    if(!nonblocking(client)){close_client();return false;}accepted_at=now;
  }
  if(now-accepted_at>=2000){close_client();return false;}
  if(!response){
    const int n=recv(client,request+received,sizeof(request)-1-received,0);
    if(n<0){if(errno!=EAGAIN&&errno!=EWOULDBLOCK)close_client();return false;}
    if(n==0){close_client();return false;}
    received+=size_t(n);request[received]=0;
    if(!std::strstr(request,"\r\n")&&received<sizeof(request)-1)return false;
    wake=true;response=std::strncmp(request,"GET ",4)==0?kNavigation:kRetry;
  }
  const size_t length=std::strlen(response);
  const int n=send(client,response+sent,length-sent,0);
  if(n<0){if(errno!=EAGAIN&&errno!=EWOULDBLOCK)close_client();return false;}
  sent+=size_t(n);if(sent<length)return false;
  const bool result=wake;shutdown(client,SHUT_WR);close_client();return result;
}
}
