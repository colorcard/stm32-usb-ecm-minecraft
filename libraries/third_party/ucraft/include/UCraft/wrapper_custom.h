#ifndef WRAPPER_CUSTOM_H
#define WRAPPER_CUSTOM_H

/*
 * UCraft 平台适配层（STM32G474 + lwIP socket API + FreeRTOS），对齐 UCraft-bl602。
 * 直接用 lwIP 的 BSD socket 接口，UCraft 的 socket/select 主循环无需改动。
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "lwip/sockets.h"
#include "lwip/netdb.h"

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
#ifndef SHUT_RDWR
#define SHUT_RDWR 2
#endif
#ifndef EWOULDBLOCK
#define EWOULDBLOCK EAGAIN
#endif

/* ------- 时间/杂项 ------- */
static inline void U_wrapperStart(void) {}
static inline void U_wrapperEnd(void) {}

static inline void U_sleep(int msec)
{
  vTaskDelay(pdMS_TO_TICKS((TickType_t)msec));
}

static inline uint64_t U_millis(void)
{
  return (uint64_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

/* ------- 网络（直接复用 lwIP socket API） ------- */
static inline int U_socket(int domain, int type, int protocol)
{
  return lwip_socket(domain, type, protocol);
}

static inline int U_setsockopt(int sockfd, int level, int optname,
                               const void *optval, socklen_t optlen)
{
  return lwip_setsockopt(sockfd, level, optname, optval, optlen);
}

static inline int U_setsocknonblock(int sockfd)
{
  return lwip_fcntl(sockfd, F_SETFL, O_NONBLOCK);
}

static inline int U_bind(int sockfd, const struct sockaddr *addr,
                         socklen_t addrlen)
{
  return lwip_bind(sockfd, addr, addrlen);
}

static inline int U_listen(int sockfd, int backlog)
{
  return lwip_listen(sockfd, backlog);
}

static inline int U_select(int nfds, fd_set *readfds, fd_set *writefds,
                           fd_set *exceptfds, struct timeval *timeout)
{
  return lwip_select(nfds, readfds, writefds, exceptfds, timeout);
}

static inline int U_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
  return lwip_accept(sockfd, addr, addrlen);
}

static inline int U_getpeername(int sockfd, struct sockaddr *addr,
                                socklen_t *addrlen)
{
  return lwip_getpeername(sockfd, addr, addrlen);
}

static inline char *U_inet_ntoa(struct in_addr in)
{
  return ip4addr_ntoa((const ip4_addr_t *)&in);
}

static inline ssize_t U_recv(int sockfd, void *buf, size_t len, int flags)
{
  return lwip_recv(sockfd, buf, len, flags);
}

static inline ssize_t U_send(int sockfd, const void *buf, size_t len, int flags)
{
  return lwip_send(sockfd, buf, len, flags);
}

static inline int U_connect(int sockfd, const struct sockaddr *addr,
                            socklen_t addrlen)
{
  return lwip_connect(sockfd, addr, addrlen);
}

static inline int U_close(int fd)
{
  return lwip_close(fd);
}

static inline int U_shutdown(int sockfd, int how)
{
  return lwip_shutdown(sockfd, how);
}

/* ------- 内存（FreeRTOS 堆，带 8 字节对齐头以支持 realloc） ------- */
#define UC_MEM_ALIGN 8U

static inline void *U_malloc(size_t size)
{
  uint8_t *base = (uint8_t *)pvPortMalloc(size + UC_MEM_ALIGN);
  if (base == NULL) {
    return NULL;
  }
  *(size_t *)base = size;
  return base + UC_MEM_ALIGN;
}

static inline void *U_calloc(size_t nmemb, size_t size)
{
  size_t total = nmemb * size;
  void *p = U_malloc(total);
  if (p != NULL) {
    memset(p, 0, total);
  }
  return p;
}

static inline void U_free(void *ptr)
{
  if (ptr != NULL) {
    vPortFree((uint8_t *)ptr - UC_MEM_ALIGN);
  }
}

static inline void *U_realloc(void *ptr, size_t size)
{
  uint8_t *base;
  size_t old;
  void *np;

  if (ptr == NULL) {
    return U_malloc(size);
  }
  base = (uint8_t *)ptr - UC_MEM_ALIGN;
  old = *(size_t *)base;
  np = U_malloc(size);
  if (np == NULL) {
    return NULL;
  }
  memcpy(np, ptr, (old < size) ? old : size);
  U_free(ptr);
  return np;
}

#endif /* WRAPPER_CUSTOM_H */
