/**
 * @file rp_netif_ecm.c
 * @brief USB CDC-ECM 网卡的 lwIP netif 驱动（全速，单帧缓冲）。
 */
#include "rp_netif_ecm.h"

#include <string.h>

#include "lwip/etharp.h"
#include "lwip/pbuf.h"
#include "netif/ethernet.h"

#include "rp_common_headfile.h"
#include "rp_device_usb_ecm.h"
#include "usbd_ecm.h"

/** @brief 发送暂存缓冲（需容纳整帧）。 */
static uint8_t s_tx_buf[ECM_MAX_FRAME_SIZE];
/** @brief 接收暂存缓冲。 */
static uint8_t s_rx_buf[ECM_MAX_FRAME_SIZE];
/** @brief 已注册网卡。 */
static struct netif *s_netif;

/**
 * @brief lwIP 发送出口：把 pbuf 链拷成连续帧后交给 USB。
 */
static err_t ecm_low_level_output(struct netif *netif, struct pbuf *p)
{
  uint16_t total = p->tot_len;

  (void)netif;

  if (total > (uint16_t)sizeof(s_tx_buf)) {
    return ERR_MEM;
  }
  if (pbuf_copy_partial(p, s_tx_buf, total, 0U) != total) {
    return ERR_BUF;
  }
  if (usb_ecm_send(s_tx_buf, total) != 0) {
    return ERR_IF;
  }
  return ERR_OK;
}

/**
 * @brief netif 初始化：绑定输出函数与 MAC。
 * @note MAC 末字节与 ECM 功能描述符里通告的 iMACAddress 故意不同：宿主会
 *       采用 iMACAddress 作为其接口 MAC，若两端相同，宿主会丢弃源 MAC 等于
 *       自身的帧（含 ARP 应答），导致无法解析。
 */
static err_t ecm_netif_init(struct netif *netif)
{
  uint32_t uid = HAL_GetUIDw0() ^ HAL_GetUIDw1() ^ HAL_GetUIDw2();

  netif->name[0] = 'e';
  netif->name[1] = 'c';
  netif->output = etharp_output;
  netif->linkoutput = ecm_low_level_output;
  netif->mtu = 1500U;
  netif->hwaddr_len = ETH_HWADDR_LEN;

  /* 本地管理地址 02:00:xx:xx:xx:xx，用 UID 保证唯一。 */
  netif->hwaddr[0] = 0x02U;
  netif->hwaddr[1] = 0x00U;
  netif->hwaddr[2] = (uint8_t)(uid >> 24);
  netif->hwaddr[3] = (uint8_t)(uid >> 16);
  netif->hwaddr[4] = (uint8_t)(uid >> 8);
  netif->hwaddr[5] = (uint8_t)((uid & 0xFFU) ^ 0x01U);

  netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP |
                 NETIF_FLAG_LINK_UP;
  return ERR_OK;
}

int rp_netif_ecm_add(struct netif *netif)
{
  if (netif_add(netif, IP4_ADDR_ANY, IP4_ADDR_ANY, IP4_ADDR_ANY, NULL,
                ecm_netif_init, ethernet_input) == NULL) {
    return -1;
  }
  s_netif = netif;
  return 0;
}

void rp_netif_ecm_poll(void)
{
  uint16_t len;

  if (s_netif == NULL) {
    return;
  }

  while (usb_ecm_frame_read(s_rx_buf, (uint16_t)sizeof(s_rx_buf), &len) == 0) {
    struct pbuf *p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);

    if (p == NULL) {
      continue;
    }
    if (pbuf_take(p, s_rx_buf, len) != ERR_OK) {
      pbuf_free(p);
      continue;
    }
    if (s_netif->input(p, s_netif) != ERR_OK) {
      pbuf_free(p);
    }
  }
}
