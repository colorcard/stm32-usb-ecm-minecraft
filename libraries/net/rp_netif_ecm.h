/**
 * @file rp_netif_ecm.h
 * @brief 把 USB CDC-ECM 网卡接入 lwIP 的 netif 驱动。
 */
#ifndef RP_NETIF_ECM_H
#define RP_NETIF_ECM_H

#include <stdint.h>

#include "lwip/netif.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 创建并注册 ECM 网卡（IP 由调用方随后设置）。
 * @param netif 由调用方提供的 netif 实例。
 * @return 0 成功，-1 失败。
 */
int rp_netif_ecm_add(struct netif *netif);

/**
 * @brief 主循环服务：把 USB 收到的以太网帧喂给 lwIP。
 * @return 无。
 */
void rp_netif_ecm_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* RP_NETIF_ECM_H */
