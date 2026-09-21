/**
 * @file usbd_ecm_if.h
 * @brief ECM 类与设备层之间的接口回调声明。
 */
#ifndef __USBD_ECM_IF_H
#define __USBD_ECM_IF_H

#ifdef __cplusplus
extern "C" {
#endif

#include "usbd_ecm.h"

/** @brief ECM 接口回调实例（注册到 ECM 类）。 */
extern USBD_ECM_ItfTypeDef USBD_Interface_fops_ECM;

/**
 * @brief 在数据 IN 端点发送一帧以太网数据。
 * @param Buf 帧缓冲（14 字节以太网头 + 负载）。
 * @param Len 帧长度。
 * @return USBD_OK / USBD_BUSY / USBD_FAIL。
 * @note 会阻塞至本次发送完成或超时。
 */
uint8_t ECM_Transmit_FS(uint8_t *Buf, uint16_t Len);

#ifdef __cplusplus
}
#endif

#endif /* __USBD_ECM_IF_H */
