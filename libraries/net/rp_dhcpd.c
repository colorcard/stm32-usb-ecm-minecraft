/**
 * @file rp_dhcpd.c
 * @brief 极简 DHCP 服务器（BOOTP 报文 + 选项 53/54/51/1）。
 *
 * 只服务单个宿主：DISCOVER -> OFFER(192.168.7.2)，REQUEST -> ACK。
 * 不包含 Router 选项，避免宿主把本设备当成默认网关。
 */
#include "rp_dhcpd.h"

#include <string.h>

#include "lwip/ip_addr.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"

/** @brief 服务器/客户端端口。 */
#define DHCPD_SERVER_PORT   67U
#define DHCPD_CLIENT_PORT   68U
/** @brief 报文构造/接收缓冲长度。 */
#define DHCPD_MSG_SIZE      576U
/** @brief 应答最小长度（BOOTP 惯例）。 */
#define DHCPD_MIN_REPLY     300U

/* 服务器与分配地址（设备 192.168.7.1，宿主 192.168.7.2）。 */
#define DHCPD_IP_A          192U
#define DHCPD_IP_B          168U
#define DHCPD_IP_C          7U
#define DHCPD_SERVER_D      1U
#define DHCPD_OFFER_D       2U
/** @brief 租约（秒）。 */
#define DHCPD_LEASE_TIME    3600U

/* DHCP/BOOTP 字段偏移 */
#define DHCP_OFF_OP         0U
#define DHCP_OFF_HTYPE      1U
#define DHCP_OFF_HLEN       2U
#define DHCP_OFF_XID        4U
#define DHCP_OFF_YIADDR     16U
#define DHCP_OFF_SIADDR     20U
#define DHCP_OFF_CHADDR     28U
#define DHCP_OFF_MAGIC      236U
#define DHCP_OFF_OPTIONS    240U

/* DHCP 消息类型 */
#define DHCPDISCOVER        1U
#define DHCPOFFER           2U
#define DHCPREQUEST         3U
#define DHCPACK             5U

static struct udp_pcb *s_pcb;
/** @brief 是否已成功分配租约（收到过 REQUEST）。 */
static volatile uint8_t s_lease_active;

static void dhcpd_put_ip(uint8_t *p, uint8_t a, uint8_t b, uint8_t c,
                         uint8_t d)
{
  p[0] = a;
  p[1] = b;
  p[2] = c;
  p[3] = d;
}

/**
 * @brief 在选项区查找指定 option 的负载。
 * @return 负载指针；未找到返回 NULL，长度写入 *len。
 */
static const uint8_t *dhcpd_find_option(const uint8_t *opts, uint16_t opts_len,
                                        uint8_t code, uint8_t *len)
{
  uint16_t i = 0U;

  while (i < opts_len) {
    uint8_t c = opts[i];
    uint8_t l;

    if (c == 0U) {
      ++i;
      continue;
    }
    if (c == 255U) {
      break;
    }
    if ((uint16_t)(i + 1U) >= opts_len) {
      break;
    }
    l = opts[i + 1U];
    if ((uint16_t)(i + 2U + l) > opts_len) {
      break;
    }
    if (c == code) {
      *len = l;
      return &opts[i + 2U];
    }
    i = (uint16_t)(i + 2U + l);
  }
  return NULL;
}

/**
 * @brief 构造 DHCP 应答（OFFER/ACK）。
 * @return 应答长度。
 */
static uint16_t dhcpd_build_reply(const uint8_t *req, uint8_t msg_type,
                                  uint8_t *out)
{
  uint16_t o = DHCP_OFF_OPTIONS;

  (void)memset(out, 0, DHCPD_MSG_SIZE);

  out[DHCP_OFF_OP] = 2U;                 /* BOOTREPLY */
  out[DHCP_OFF_HTYPE] = req[DHCP_OFF_HTYPE];
  out[DHCP_OFF_HLEN] = req[DHCP_OFF_HLEN];
  (void)memcpy(&out[DHCP_OFF_XID], &req[DHCP_OFF_XID], 4U);
  dhcpd_put_ip(&out[DHCP_OFF_YIADDR], DHCPD_IP_A, DHCPD_IP_B, DHCPD_IP_C,
               DHCPD_OFFER_D);
  dhcpd_put_ip(&out[DHCP_OFF_SIADDR], DHCPD_IP_A, DHCPD_IP_B, DHCPD_IP_C,
               DHCPD_SERVER_D);
  (void)memcpy(&out[DHCP_OFF_CHADDR], &req[DHCP_OFF_CHADDR], 16U);

  out[DHCP_OFF_MAGIC] = 99U;
  out[DHCP_OFF_MAGIC + 1U] = 130U;
  out[DHCP_OFF_MAGIC + 2U] = 83U;
  out[DHCP_OFF_MAGIC + 3U] = 99U;

  out[o++] = 53U;                        /* DHCP Message Type */
  out[o++] = 1U;
  out[o++] = msg_type;

  out[o++] = 54U;                        /* Server Identifier */
  out[o++] = 4U;
  dhcpd_put_ip(&out[o], DHCPD_IP_A, DHCPD_IP_B, DHCPD_IP_C, DHCPD_SERVER_D);
  o += 4U;

  out[o++] = 51U;                        /* IP Address Lease Time */
  out[o++] = 4U;
  out[o++] = (uint8_t)(DHCPD_LEASE_TIME >> 24);
  out[o++] = (uint8_t)(DHCPD_LEASE_TIME >> 16);
  out[o++] = (uint8_t)(DHCPD_LEASE_TIME >> 8);
  out[o++] = (uint8_t)DHCPD_LEASE_TIME;

  out[o++] = 1U;                         /* Subnet Mask */
  out[o++] = 4U;
  dhcpd_put_ip(&out[o], 255U, 255U, 255U, 0U);
  o += 4U;

  out[o++] = 255U;                       /* End */
  return o;
}

static void dhcpd_send_reply(struct udp_pcb *pcb, const uint8_t *req,
                             uint8_t msg_type)
{
  static uint8_t s_reply[DHCPD_MSG_SIZE];
  ip_addr_t dst;
  struct pbuf *p;
  uint16_t len;

  len = dhcpd_build_reply(req, msg_type, s_reply);
  if (len < DHCPD_MIN_REPLY) {
    len = DHCPD_MIN_REPLY;
  }
  p = pbuf_alloc(PBUF_TRANSPORT, len, PBUF_RAM);
  if (p == NULL) {
    return;
  }
  (void)pbuf_take(p, s_reply, len);

  IP4_ADDR(&dst, 255U, 255U, 255U, 255U);
  (void)udp_sendto(pcb, p, &dst, DHCPD_CLIENT_PORT);
  pbuf_free(p);
}

static void dhcpd_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                       const ip_addr_t *addr, u16_t port)
{
  uint8_t buf[DHCPD_MSG_SIZE];
  uint8_t opt_len = 0U;
  const uint8_t *mt;
  uint16_t total;

  (void)arg;
  (void)addr;
  (void)port;

  if (p == NULL) {
    return;
  }
  total = p->tot_len;
  if ((total < DHCP_OFF_OPTIONS) || (total > sizeof(buf))) {
    pbuf_free(p);
    return;
  }
  (void)pbuf_copy_partial(p, buf, total, 0U);
  pbuf_free(p);

  if ((buf[DHCP_OFF_OP] != 1U) || (buf[DHCP_OFF_MAGIC] != 99U)) {
    return;
  }

  mt = dhcpd_find_option(&buf[DHCP_OFF_OPTIONS],
                         (uint16_t)(total - DHCP_OFF_OPTIONS), 53U, &opt_len);
  if ((mt == NULL) || (opt_len != 1U)) {
    return;
  }

  if (mt[0] == DHCPDISCOVER) {
    dhcpd_send_reply(pcb, buf, DHCPOFFER);
  } else if (mt[0] == DHCPREQUEST) {
    dhcpd_send_reply(pcb, buf, DHCPACK);
    s_lease_active = 1U;
  }
}

int rp_dhcpd_init(void)
{
  s_pcb = udp_new();
  if (s_pcb == NULL) {
    return -1;
  }
  if (udp_bind(s_pcb, IP_ADDR_ANY, DHCPD_SERVER_PORT) != ERR_OK) {
    udp_remove(s_pcb);
    s_pcb = NULL;
    return -1;
  }
  ip_set_option(s_pcb, SOF_BROADCAST);
  udp_recv(s_pcb, dhcpd_recv, NULL);
  return 0;
}

int rp_dhcpd_lease_active(void)
{
  return (s_lease_active != 0U) ? 1 : 0;
}
