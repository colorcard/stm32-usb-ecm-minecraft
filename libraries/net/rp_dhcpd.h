/**
 * @file rp_dhcpd.h
 * @brief 极简 DHCP 服务器：给 USB-ECM 宿主自动分配 192.168.7.2。
 */
#ifndef RP_DHCPD_H
#define RP_DHCPD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 启动 DHCP 服务器（需在 lwIP 初始化后调用）。
 * @return 0 成功，-1 失败。
 */
int rp_dhcpd_init(void);

/**
 * @brief 是否已成功向宿主分配地址（收到过 REQUEST 并回 ACK）。
 * @return 1 表示已分配，0 表示尚未。
 */
int rp_dhcpd_lease_active(void);

#ifdef __cplusplus
}
#endif

#endif /* RP_DHCPD_H */
