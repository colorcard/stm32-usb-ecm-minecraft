/**
 * @file rp_lwip.c
 * @brief lwIP 裸机移植：时间基准、初始化与静态 IP。
 */
#include "rp_lwip.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "lwip/init.h"
#include "lwip/ip_addr.h"
#include "lwip/tcpip.h"
#include "lwip/timeouts.h"

#include "rp_common_headfile.h"

#include "rp_device_usb_ecm.h"
#include "rp_dhcpd.h"
#include "rp_netif_ecm.h"

/** @brief 唯一网卡实例。 */
static struct netif s_netif;

void rp_lwip_diag_printf(const char *fmt, ...)
{
  va_list args;

  va_start(args, fmt);
  (void)vfprintf(stdout, fmt, args);
  va_end(args);
}

void rp_lwip_assert_failed(const char *msg, const char *file, int line)
{
  (void)msg;
  (void)file;
  (void)line;
  error_handler();
}

/**
 * @brief lwIP 毫秒时间基准由 sys_arch.c 提供（FreeRTOS tick）。
 */

/** @brief 用芯片 UID 初始化伪随机种子（lwIP 端口选择等）。 */
static void seed_rand(void)
{
  uint32_t seed = HAL_GetUIDw0() ^ HAL_GetUIDw1() ^ HAL_GetUIDw2();
  srand(seed);
}

struct netif *rp_lwip_init(void)
{
  ip4_addr_t ip;
  ip4_addr_t mask;

  seed_rand();
  tcpip_init(NULL, NULL);

  if (rp_netif_ecm_add(&s_netif) != 0) {
    return NULL;
  }

  IP4_ADDR(&ip, RP_NET_IP0, RP_NET_IP1, RP_NET_IP2, RP_NET_IP3);
  IP4_ADDR(&mask, RP_NET_MASK0, RP_NET_MASK1, RP_NET_MASK2, RP_NET_MASK3);
  netif_set_addr(&s_netif, &ip, &mask, &ip);
  netif_set_default(&s_netif);
  netif_set_link_up(&s_netif);
  netif_set_up(&s_netif);

  (void)rp_dhcpd_init();

  return &s_netif;
}

void rp_lwip_poll(void)
{
  usb_ecm_poll();
  rp_netif_ecm_poll();
}

struct netif *rp_lwip_netif(void)
{
  return &s_netif;
}
