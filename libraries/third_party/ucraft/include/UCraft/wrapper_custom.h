#ifndef WRAPPER_CUSTOM_H
#define WRAPPER_CUSTOM_H

/*
 * UCraft 平台适配层（STM32G474 + lwIP raw TCP）。
 *
 * 用很小的 fd 抽象把 UCraft 的 BSD socket 调用映射到 lwIP：
 *   - fd 0 固定为监听套接字；fd 1..N 为已接受的连接。
 *   - U_select() 内部驱动 lwIP，并把“有数据/有新连接”的 fd 置位。
 * 具体实现在 project/code/ucraft_platform.c。
 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
#ifndef SHUT_RDWR
#define SHUT_RDWR 2
#endif
#ifndef EWOULDBLOCK
#define EWOULDBLOCK EAGAIN
#endif

/* ------- 最小 socket 类型/常量（UCraft 仅用到少量） ------- */
typedef unsigned int socklen_t;

struct in_addr
{
  uint32_t s_addr;
};

struct sockaddr
{
  unsigned short sa_family;
  char sa_data[14];
};

struct sockaddr_in
{
  unsigned short sin_family;
  unsigned short sin_port;
  struct in_addr sin_addr;
  char sin_zero[8];
};

#define AF_INET 2
#define SOCK_STREAM 1
#define SOL_SOCKET 1
#define SO_REUSEADDR 2
#define IPPROTO_TCP 6
#define TCP_NODELAY 1
#define INADDR_ANY 0U

static inline uint16_t U_htons(uint16_t x)
{
  return (uint16_t)((x >> 8) | (x << 8));
}
#ifndef htons
#define htons(x) U_htons((uint16_t)(x))
#endif
#ifndef ntohs
#define ntohs(x) U_htons((uint16_t)(x))
#endif

/* ------- fd_set 使用 newlib <sys/select.h> 的定义 ------- */

/* ------- 时间/杂项 ------- */
void U_wrapperStart(void);
void U_wrapperEnd(void);
void U_sleep(int msec);
uint64_t U_millis(void);

/* ------- 网络 ------- */
int U_socket(int domain, int type, int protocol);
int U_setsockopt(int fd, int level, int optname, const void *optval,
                 socklen_t optlen);
int U_setsocknonblock(int fd);
int U_bind(int fd, const struct sockaddr *addr, socklen_t addrlen);
int U_listen(int fd, int backlog);
int U_select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
             struct timeval *timeout);
int U_accept(int fd, struct sockaddr *addr, socklen_t *addrlen);
int U_getpeername(int fd, struct sockaddr *addr, socklen_t *addrlen);
char *U_inet_ntoa(struct in_addr in);
ssize_t U_recv(int fd, void *buf, size_t len, int flags);
ssize_t U_send(int fd, const void *buf, size_t len, int flags);
int U_close(int fd);
int U_shutdown(int fd, int how);

/* ------- 内存 ------- */
void *U_malloc(size_t size);
void *U_calloc(size_t nmemb, size_t size);
void *U_realloc(void *ptr, size_t size);
void U_free(void *ptr);

/* 供平台层在 lwIP 回调中投递连接/数据。 */
void ucPlatformOnAccept(int fd);
void ucPlatformOnData(int fd, const uint8_t *data, uint16_t len);
void ucPlatformOnClose(int fd);
int ucPlatformInit(void);
void ucPlatformPoll(void);

#endif /* WRAPPER_CUSTOM_H */
