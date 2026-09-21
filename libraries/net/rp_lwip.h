/**
 * @file rp_lwip.h
 * @brief lwIP 裸机移植入口（时间基准、初始化、静态 IP 配置）。
 */
#ifndef RP_LWIP_H
#define RP_LWIP_H

#include <stdint.h>

#include "lwip/netif.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 设备侧静态 IP（由内置 DHCP 分配给宿主 192.168.7.2）。
 */
#define RP_NET_IP0            192U
#define RP_NET_IP1            168U
#define RP_NET_IP2            7U
#define RP_NET_IP3            1U
/** @brief 设备侧子网掩码 255.255.255.0。 */
#define RP_NET_MASK0          255U
#define RP_NET_MASK1          255U
#define RP_NET_MASK2          255U
#define RP_NET_MASK3          0U

/**
 * @brief 初始化 lwIP 并挂载 USB ECM 网卡（静态 IP）。
 * @return 网卡指针，失败返回 NULL。
 */
struct netif *rp_lwip_init(void);

/**
 * @brief 主循环服务函数：处理 lwIP 定时器与待发数据。
 * @return 无。
 */
void rp_lwip_poll(void);

/** @brief 当前网卡（供 MC 服务器引用）。 */
struct netif *rp_lwip_netif(void);

#ifdef __cplusplus
}
#endif

#endif /* RP_LWIP_H */
