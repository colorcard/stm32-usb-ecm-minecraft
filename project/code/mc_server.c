/**
 * @file mc_server.c
 * @brief Minecraft Java 版协议的最小服务端实现。
 *
 * 支持：Handshake -> Status Request/Response + Ping/Pong，以及离线模式
 * Login Start -> Login Success。运行在 lwIP raw TCP API 之上，裸机无 RTOS。
 */
#include "mc_server.h"

#include <stdio.h>
#include <string.h>

#include "lwip/tcp.h"

/** @brief 单连接接收缓冲（握手/登录报文很小）。 */
#define MC_RX_BUF        512U
/** @brief 同时支持的连接数。 */
#define MC_MAX_CONN      2U
/** @brief 连接状态。 */
#define MC_ST_HANDSHAKE  0U
#define MC_ST_STATUS     1U
#define MC_ST_LOGIN      2U

/** @brief 单条连接上下文。 */
typedef struct
{
  struct tcp_pcb *pcb;
  uint8_t  buf[MC_RX_BUF];
  uint16_t len;
  uint8_t  state;
  uint32_t protocol;
  uint8_t  in_use;
} mc_conn_t;

static struct tcp_pcb *s_listen;
static mc_conn_t s_conn[MC_MAX_CONN];

/* ------------------------------ VarInt 工具 ------------------------------ */

static int mc_varint_encode(uint32_t value, uint8_t *out)
{
  int i = 0;

  do {
    uint8_t byte = (uint8_t)(value & 0x7FU);
    value >>= 7;
    if (value != 0U) {
      byte |= 0x80U;
    }
    out[i++] = byte;
  } while (value != 0U);
  return i;
}

/**
 * @brief 解码 VarInt。
 * @return 已消耗字节数（>0）；0 表示需要更多数据；-1 表示非法。
 */
static int mc_varint_decode(const uint8_t *p, uint32_t max, uint32_t *value)
{
  uint32_t result = 0U;
  uint32_t shift = 0U;
  uint32_t i;

  for (i = 0U; i < 5U; ++i) {
    uint8_t byte;
    if (i >= max) {
      return 0;
    }
    byte = p[i];
    result |= (uint32_t)(byte & 0x7FU) << shift;
    if ((byte & 0x80U) == 0U) {
      *value = result;
      return (int)(i + 1U);
    }
    shift += 7U;
  }
  return -1;
}

static void mc_conn_close(mc_conn_t *c)
{
  c->in_use = 0U;
  c->len = 0U;
  c->state = MC_ST_HANDSHAKE;
  if (c->pcb != NULL) {
    tcp_arg(c->pcb, NULL);
    tcp_recv(c->pcb, NULL);
    tcp_err(c->pcb, NULL);
    tcp_close(c->pcb);
    c->pcb = NULL;
  }
}

/**
 * @brief 发送一个已组装好的报文（payload 含 packet id）。
 */
static void mc_send(mc_conn_t *c, const uint8_t *payload, uint16_t len)
{
  uint8_t hdr[5];
  int hn;

  if ((c->pcb == NULL) || (len == 0U)) {
    return;
  }
  hn = mc_varint_encode(len, hdr);
  if (tcp_write(c->pcb, hdr, (uint16_t)hn, TCP_WRITE_FLAG_COPY) != ERR_OK) {
    return;
  }
  (void)tcp_write(c->pcb, payload, len, TCP_WRITE_FLAG_COPY);
  (void)tcp_output(c->pcb);
}

/** @brief String（VarInt 长度 + UTF-8）写入缓冲。 */
static int mc_put_string(uint8_t *out, const char *s)
{
  uint16_t n = (uint16_t)strlen(s);
  int o = mc_varint_encode(n, out);

  (void)memcpy(&out[o], s, n);
  return o + (int)n;
}

/** @brief 读取 String，返回消耗字节数，<=0 表示失败。 */
static int mc_get_string(const uint8_t *p, uint32_t max, char *out,
                         uint16_t out_size)
{
  uint32_t n;
  int hn = mc_varint_decode(p, max, &n);

  if (hn <= 0) {
    return -1;
  }
  if (n >= out_size) {
    return -1;
  }
  if ((uint32_t)hn + n > max) {
    return -1;
  }
  (void)memcpy(out, &p[hn], n);
  out[n] = '\0';
  return hn + (int)n;
}

/* ------------------------------ 报文处理 ------------------------------ */

/**
 * @brief 组装并发送 Status Response。
 */
static void mc_send_status_response(mc_conn_t *c)
{
  uint8_t out[384];
  char json[256];
  int jl;
  int o = 0;

  jl = snprintf(json, sizeof(json),
                "{\"version\":{\"name\":\"Rapfi-STM32\",\"protocol\":%lu},"
                "\"players\":{\"max\":8,\"online\":0,\"sample\":[]},"
                "\"description\":{\"text\":\"Rapfi STM32 USB-Ethernet\"}}",
                (unsigned long)c->protocol);
  if (jl <= 0) {
    return;
  }

  out[o++] = 0x00U;                 /* packet id: Status Response */
  o += mc_put_string(&out[o], json);
  mc_send(c, out, (uint16_t)o);
}

/**
 * @brief 原样回送 Ping 负载作为 Pong。
 */
static void mc_send_pong(mc_conn_t *c, const uint8_t *payload, uint32_t len)
{
  uint8_t out[16];
  int o = 0;

  if (len < 8U) {
    return;
  }
  out[o++] = 0x01U;                 /* packet id: Pong */
  (void)memcpy(&out[o], payload, 8U);
  o += 8;
  mc_send(c, out, (uint16_t)o);
}

/**
 * @brief 由用户名派生一个稳定的离线 UUID。
 */
static void mc_fill_uuid(const char *name, uint8_t uuid[16])
{
  uint32_t h0 = 2166136261UL;
  uint32_t h1 = 0x811C9DC5UL;
  const char *p;

  for (p = name; *p != '\0'; ++p) {
    h0 = (h0 ^ (uint8_t)*p) * 16777619UL;
    h1 = (h1 + (uint8_t)*p) * 2246822519UL;
  }
  (void)memcpy(uuid, &h0, 4U);
  (void)memcpy(&uuid[4], &h1, 4U);
  (void)memcpy(&uuid[8], &h0, 4U);
  (void)memcpy(&uuid[12], &h1, 4U);
  uuid[6] = (uint8_t)((uuid[6] & 0x0FU) | 0x30U); /* version 3 */
  uuid[8] = (uint8_t)((uuid[8] & 0x3FU) | 0x80U); /* variant */
}

static void mc_uuid_to_string(const uint8_t uuid[16], char out[37])
{
  (void)snprintf(out, 37,
                 "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-"
                 "%02x%02x%02x%02x%02x%02x",
                 uuid[0], uuid[1], uuid[2], uuid[3], uuid[4], uuid[5],
                 uuid[6], uuid[7], uuid[8], uuid[9], uuid[10], uuid[11],
                 uuid[12], uuid[13], uuid[14], uuid[15]);
}

/**
 * @brief 发送 Login Success（离线模式）。
 */
static void mc_send_login_success(mc_conn_t *c, const char *name)
{
  uint8_t uuid[16];
  uint8_t out[128];
  int o = 0;

  mc_fill_uuid(name, uuid);

  out[o++] = 0x02U;                 /* packet id: Login Success */
  if (c->protocol >= 735U) {
    /* 1.16+：16 字节 UUID */
    (void)memcpy(&out[o], uuid, 16U);
    o += 16;
  } else {
    /* 旧版：带连字符的字符串 UUID */
    char us[37];
    mc_uuid_to_string(uuid, us);
    o += mc_put_string(&out[o], us);
  }
  o += mc_put_string(&out[o], name);
  out[o++] = 0x00U;                 /* properties 数组长度 = 0 */
  mc_send(c, out, (uint16_t)o);
}

/**
 * @brief 处理一个完整报文。
 */
static void mc_handle_packet(mc_conn_t *c, const uint8_t *p, uint32_t len)
{
  uint32_t id;
  int n = mc_varint_decode(p, len, &id);

  if (n <= 0) {
    return;
  }
  p += n;
  len -= (uint32_t)n;

  if (c->state == MC_ST_HANDSHAKE) {
    uint32_t protocol;
    uint32_t next;
    char addr[128];
    int hn;

    if (id != 0x00U) {
      return;
    }
    hn = mc_varint_decode(p, len, &protocol);
    if (hn <= 0) {
      return;
    }
    p += hn;
    len -= (uint32_t)hn;
    hn = mc_get_string(p, len, addr, sizeof(addr));
    if (hn <= 0) {
      return;
    }
    p += hn;
    len -= (uint32_t)hn;
    if (len < 2U) {
      return;
    }
    p += 2U;                        /* 跳过 server port */
    len -= 2U;
    hn = mc_varint_decode(p, len, &next);
    if (hn <= 0) {
      return;
    }
    c->protocol = protocol;
    c->state = (next == 1U) ? MC_ST_STATUS : MC_ST_LOGIN;
    return;
  }

  if (c->state == MC_ST_STATUS) {
    if (id == 0x00U) {
      mc_send_status_response(c);
    } else if (id == 0x01U) {
      mc_send_pong(c, p, len);
    }
    return;
  }

  if (c->state == MC_ST_LOGIN) {
    if (id == 0x00U) {
      char name[32];
      if (mc_get_string(p, len, name, sizeof(name)) > 0) {
        mc_send_login_success(c, name);
      }
    }
    return;
  }
}

/**
 * @brief 从接收缓冲中解析出尽可能多的完整报文。
 */
static void mc_drain(mc_conn_t *c)
{
  while (c->len != 0U) {
    uint32_t plen;
    int hn = mc_varint_decode(c->buf, c->len, &plen);
    uint32_t total;

    if (hn == 0) {
      return;                       /* 长度字段未收全 */
    }
    if (hn < 0) {
      mc_conn_close(c);
      return;
    }
    total = (uint32_t)hn + plen;
    if (total > MC_RX_BUF) {
      mc_conn_close(c);             /* 异常大报文 */
      return;
    }
    if (c->len < total) {
      return;                       /* 报文体未收全 */
    }
    mc_handle_packet(c, &c->buf[hn], plen);
    if (c->pcb == NULL) {
      return;
    }
    (void)memmove(c->buf, &c->buf[total], c->len - total);
    c->len = (uint16_t)(c->len - total);
  }
}

/* ------------------------------ lwIP 回调 ------------------------------ */

static err_t mc_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
  mc_conn_t *c = (mc_conn_t *)arg;

  (void)pcb;
  if (c == NULL) {
    if (p != NULL) {
      pbuf_free(p);
    }
    return ERR_OK;
  }
  if ((err != ERR_OK) || (p == NULL)) {
    mc_conn_close(c);
    return ERR_OK;
  }

  tcp_recved(pcb, p->tot_len);

  if ((uint32_t)c->len + p->tot_len > MC_RX_BUF) {
    pbuf_free(p);
    mc_conn_close(c);
    return ERR_OK;
  }
  (void)pbuf_copy_partial(p, &c->buf[c->len], p->tot_len, 0U);
  c->len = (uint16_t)(c->len + p->tot_len);
  pbuf_free(p);

  mc_drain(c);
  return ERR_OK;
}

static void mc_err(void *arg, err_t err)
{
  mc_conn_t *c = (mc_conn_t *)arg;

  (void)err;
  if (c != NULL) {
    c->pcb = NULL;                  /* 协议栈已释放 pcb */
    c->in_use = 0U;
    c->len = 0U;
    c->state = MC_ST_HANDSHAKE;
  }
}

static err_t mc_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
  uint32_t i;

  (void)arg;
  if ((err != ERR_OK) || (newpcb == NULL)) {
    return ERR_VAL;
  }

  for (i = 0U; i < MC_MAX_CONN; ++i) {
    if (s_conn[i].in_use == 0U) {
      mc_conn_t *c = &s_conn[i];
      (void)memset(c, 0, sizeof(*c));
      c->pcb = newpcb;
      c->state = MC_ST_HANDSHAKE;
      c->in_use = 1U;
      tcp_arg(newpcb, c);
      tcp_recv(newpcb, mc_recv);
      tcp_err(newpcb, mc_err);
      tcp_nagle_disable(newpcb);
      return ERR_OK;
    }
  }

  tcp_abort(newpcb);
  return ERR_ABRT;
}

rp_status_t mc_server_init(void)
{
  struct tcp_pcb *pcb;

  (void)memset(s_conn, 0, sizeof(s_conn));

  pcb = tcp_new();
  if (pcb == NULL) {
    return RP_ERROR;
  }
  if (tcp_bind(pcb, IP_ADDR_ANY, MC_SERVER_PORT) != ERR_OK) {
    tcp_close(pcb);
    return RP_ERROR;
  }
  s_listen = tcp_listen(pcb);
  if (s_listen == NULL) {
    tcp_close(pcb);
    return RP_ERROR;
  }
  tcp_accept(s_listen, mc_accept);
  return RP_OK;
}

void mc_server_poll(void)
{
  /* 目前无需周期动作；保留接口便于后续加超时回收。 */
}
