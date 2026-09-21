/**
 * @file ucrafr_platform.c
 * @brief UCraft 在 STM32 + lwIP(raw TCP) 上的平台适配。
 *
 * UCraft 使用 BSD socket + select；这里用极简 fd 抽象映射到 lwIP：
 *   fd 0 = 监听套接字，fd 1..N = 已接受连接。
 * U_select() 内部驱动 lwIP，并把就绪的 fd 置位，UCraft 主循环无需改动。
 */
#include "wrapper_custom.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "lwip/tcp.h"

#include "rp_common_headfile.h"
#include "rp_lwip.h"
#include "server_display.h"

/********** UCraft 需求的最小 POSIX 兼容 **********/
#define UC_MAX_CONN 4U
#define UC_RX_SIZE 4096U

typedef struct
{
  struct tcp_pcb *pcb;
  uint8_t rx[UC_RX_SIZE];
  volatile uint16_t head;
  volatile uint16_t tail;
  uint8_t used;
  uint8_t closed;
} uc_conn_t;

static struct tcp_pcb *s_listener;
static uc_conn_t s_conn[UC_MAX_CONN];
/** @brief 待 U_accept 取走的连接 fd 队列。 */
static int s_accept_q[UC_MAX_CONN];
static volatile uint8_t s_accept_head;
static volatile uint8_t s_accept_tail;

/********** 时间/杂项 **********/
void U_wrapperStart(void) {}
void U_wrapperEnd(void) {}

void U_sleep(int msec)
{
  HAL_Delay((uint32_t)msec);
}

uint64_t U_millis(void)
{
  return (uint64_t)HAL_GetTick();
}

/********** 内存 **********/
void *U_malloc(size_t size)
{
  return malloc(size);
}
void *U_calloc(size_t nmemb, size_t size)
{
  return calloc(nmemb, size);
}
void *U_realloc(void *ptr, size_t size)
{
  return realloc(ptr, size);
}
void U_free(void *ptr)
{
  free(ptr);
}

/********** lwIP 回调 **********/
static void uc_recv_cb(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
  uc_conn_t *c = (uc_conn_t *)arg;

  (void)pcb;
  if (c == NULL) {
    if (p != NULL) {
      pbuf_free(p);
    }
    return;
  }
  if ((err != ERR_OK) || (p == NULL)) {
    c->closed = 1U;
    return;
  }

  tcp_recved(pcb, p->tot_len);
  if (p->tot_len <= UC_RX_SIZE) {
    uint16_t total = p->tot_len;
    struct pbuf *q;
    for (q = p; q != NULL; q = q->next) {
      uint16_t i;
      for (i = 0U; i < q->len; ++i) {
        uint16_t next = (uint16_t)((c->head + 1U) % UC_RX_SIZE);
        if (next == c->tail) {
          break; /* 满，丢弃 */
        }
        c->rx[c->head] = ((const uint8_t *)q->payload)[i];
        c->head = next;
      }
    }
    (void)total;
  }
  pbuf_free(p);
}

static void uc_err_cb(void *arg, err_t err)
{
  uc_conn_t *c = (uc_conn_t *)arg;

  (void)err;
  if (c != NULL) {
    c->pcb = NULL;
    c->closed = 1U;
  }
}

static err_t uc_accept_cb(void *arg, struct tcp_pcb *newpcb, err_t err)
{
  uint32_t i;

  (void)arg;
  if ((err != ERR_OK) || (newpcb == NULL)) {
    return ERR_VAL;
  }

  for (i = 0U; i < UC_MAX_CONN; ++i) {
    if (s_conn[i].used == 0U) {
      uc_conn_t *c = &s_conn[i];
      c->pcb = newpcb;
      c->head = 0U;
      c->tail = 0U;
      c->used = 1U;
      c->closed = 0U;
      tcp_arg(newpcb, c);
      tcp_recv(newpcb, uc_recv_cb);
      tcp_err(newpcb, uc_err_cb);
      tcp_nagle_disable(newpcb);
      s_accept_q[s_accept_head] = (int)(i + 1U);
      s_accept_head = (uint8_t)((s_accept_head + 1U) % UC_MAX_CONN);
      return ERR_OK;
    }
  }

  tcp_abort(newpcb);
  return ERR_ABRT;
}

/********** socket 兼容 **********/
int U_socket(int domain, int type, int protocol)
{
  (void)domain;
  (void)type;
  (void)protocol;
  return 0; /* 监听 fd 固定为 0（pcb 在 U_bind 初始化） */
}

int U_setsockopt(int fd, int level, int optname, const void *optval,
                 socklen_t optlen)
{
  (void)fd;
  (void)level;
  (void)optname;
  (void)optval;
  (void)optlen;
  return 0;
}

int U_setsocknonblock(int fd)
{
  (void)fd;
  return 0;
}

int U_bind(int fd, const struct sockaddr *addr, socklen_t addrlen)
{
  (void)fd;
  (void)addrlen;
  (void)addr;
  s_listener = tcp_new();
  if (s_listener == NULL) {
    return -1;
  }
  if (tcp_bind(s_listener, IP_ADDR_ANY, 25565U) != ERR_OK) {
    tcp_close(s_listener);
    s_listener = NULL;
    return -1;
  }
  return 0;
}

int U_listen(int fd, int backlog)
{
  struct tcp_pcb *l;

  (void)fd;
  (void)backlog;
  if (s_listener == NULL) {
    return -1;
  }
  l = tcp_listen(s_listener);
  if (l == NULL) {
    return -1;
  }
  s_listener = l;
  tcp_accept(s_listener, uc_accept_cb);
  return 0;
}

int U_accept(int fd, struct sockaddr *addr, socklen_t *addrlen)
{
  (void)fd;
  (void)addr;
  (void)addrlen;
  if (s_accept_head == s_accept_tail) {
    return -1;
  }
  {
    int cfd = s_accept_q[s_accept_tail];
    s_accept_tail = (uint8_t)((s_accept_tail + 1U) % UC_MAX_CONN);
    return cfd;
  }
}

int U_getpeername(int fd, struct sockaddr *addr, socklen_t *addrlen)
{
  (void)fd;
  if ((addr != NULL) && (addrlen != NULL) && (*addrlen >= sizeof(struct sockaddr_in))) {
    memset(addr, 0, sizeof(struct sockaddr_in));
  }
  return 0;
}

char *U_inet_ntoa(struct in_addr in)
{
  static char s[16];
  (void)in;
  (void)snprintf(s, sizeof(s), "usb0");
  return s;
}

/** @brief 环形缓冲中可用字节数。 */
static uint16_t uc_ring_len(const uc_conn_t *c)
{
  return (uint16_t)((c->head + UC_RX_SIZE - c->tail) % UC_RX_SIZE);
}

static uint8_t uc_ring_at(const uc_conn_t *c, uint16_t idx)
{
  return c->rx[(uint16_t)((c->tail + idx) % UC_RX_SIZE)];
}

/**
 * @brief 判断队首是否已攒够一个完整 Minecraft 包（VarInt 长度 + 负载）。
 * @return 1 且 *pkt_total 为整包字节数；否则 0。
 */
static int uc_packet_complete(const uc_conn_t *c, uint16_t *pkt_total)
{
  uint16_t avail = uc_ring_len(c);
  uint32_t len = 0U;
  uint32_t shift = 0U;
  uint16_t i;

  if (avail == 0U) {
    return 0;
  }
  for (i = 0U; i < 5U; ++i) {
    uint8_t b;
    if (i >= avail) {
      return 0;
    }
    b = uc_ring_at(c, i);
    len |= (uint32_t)(b & 0x7FU) << shift;
    if ((b & 0x80U) == 0U) {
      break;
    }
    shift += 7U;
  }
  if (i == 5U) {
    return 0;
  }
  {
    uint32_t total = (uint32_t)(i + 1U) + len;
    if ((total == 0U) || (total > UC_RX_SIZE) || (avail < total)) {
      return 0;
    }
    *pkt_total = (uint16_t)total;
  }
  return 1;
}

ssize_t U_recv(int fd, void *buf, size_t len, int flags)
{
  uc_conn_t *c;
  size_t n = 0U;
  uint8_t *out = (uint8_t *)buf;

  (void)flags;
  if ((fd < 1) || (fd > (int)UC_MAX_CONN)) {
    return -1;
  }
  c = &s_conn[fd - 1];
  if (c->used == 0U) {
    return 0;
  }
  /* 只投递完整的 Minecraft 包，避免把包切在中间导致解析错位。 */
  for (;;) {
    uint16_t tot;
    uint16_t k;
    if (uc_packet_complete(c, &tot) == 0) {
      break;
    }
    if (n + tot > len) {
      break;
    }
    for (k = 0U; k < tot; ++k) {
      out[n++] = uc_ring_at(c, 0U);
      c->tail = (uint16_t)((c->tail + 1U) % UC_RX_SIZE);
    }
  }
  if ((n == 0U) && (c->closed != 0U)) {
    return 0;
  }
  if (n == 0U) {
    errno = EAGAIN;
    return -1;
  }
  return (ssize_t)n;
}

ssize_t U_send(int fd, const void *buf, size_t len, int flags)
{
  uc_conn_t *c;
  uint16_t space;
  uint16_t n;

  (void)flags;
  if ((fd < 1) || (fd > (int)UC_MAX_CONN)) {
    return -1;
  }
  c = &s_conn[fd - 1];
  if ((c->used == 0U) || (c->pcb == NULL)) {
    errno = EAGAIN;
    return -1;
  }
  space = tcp_sndbuf(c->pcb);
  if (space == 0U) {
    errno = EAGAIN;
    return -1;
  }
  n = (len < (size_t)space) ? (uint16_t)len : space;
  if (tcp_write(c->pcb, buf, n, TCP_WRITE_FLAG_COPY) != ERR_OK) {
    errno = EAGAIN;
    return -1;
  }
  (void)tcp_output(c->pcb);
  return (ssize_t)n;
}

static void uc_conn_release(uc_conn_t *c)
{
  if (c->pcb != NULL) {
    tcp_arg(c->pcb, NULL);
    tcp_recv(c->pcb, NULL);
    tcp_err(c->pcb, NULL);
    tcp_close(c->pcb);
    c->pcb = NULL;
  }
  c->used = 0U;
  c->closed = 0U;
  c->head = 0U;
  c->tail = 0U;
}

int U_close(int fd)
{
  if ((fd >= 1) && (fd <= (int)UC_MAX_CONN)) {
    uc_conn_release(&s_conn[fd - 1]);
  }
  return 0;
}

int U_shutdown(int fd, int how)
{
  (void)how;
  return U_close(fd);
}

/********** 驱动 **********/
void ucPlatformPoll(void)
{
  rp_lwip_poll();
  server_display_poll();
}

int U_select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
             struct timeval *timeout)
{
  uint32_t i;
  int ready = 0;

  (void)nfds;
  (void)writefds;
  (void)exceptfds;
  (void)timeout;

  if (readfds == NULL) {
    return 0;
  }
  FD_ZERO(readfds);

  ucPlatformPoll();

  if (s_accept_head != s_accept_tail) {
    FD_SET(0, readfds);
    ready++;
  }
  for (i = 0U; i < UC_MAX_CONN; ++i) {
    uint16_t tot;
    if ((s_conn[i].used != 0U) &&
        ((s_conn[i].closed != 0U) || (uc_packet_complete(&s_conn[i], &tot) != 0))) {
      FD_SET((int)(i + 1U), readfds);
      ready++;
    }
  }
  return ready;
}

int ucPlatformInit(void)
{
  memset(s_conn, 0, sizeof(s_conn));
  s_accept_head = 0U;
  s_accept_tail = 0U;
  s_listener = NULL;
  return 0;
}

/* 供在别处（如中断/调试）调用，保留接口。 */
void ucPlatformOnAccept(int fd) { (void)fd; }
void ucPlatformOnData(int fd, const uint8_t *data, uint16_t len)
{
  (void)fd;
  (void)data;
  (void)len;
}
void ucPlatformOnClose(int fd) { (void)fd; }
