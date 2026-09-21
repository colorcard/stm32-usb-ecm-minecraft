#ifndef _rp_device_usb_ecm_h_
#define _rp_device_usb_ecm_h_

#include "rp_common_bsp.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 Type-C 原生 USB（USB FS Device + CDC-ECM 以太网卡）。
 * @return RP_OK 表示成功。
 */
rp_status_t usb_ecm_init(void);

/**
 * @brief USB 低优先级中断处理，转由 PCD 处理。
 * @return 无。
 */
void usb_ecm_irq_handler(void);

/**
 * @brief 周期服务：枚举完成通知主机链路已建立。
 * @return 无。
 */
void usb_ecm_poll(void);

/**
 * @brief 取出一帧待处理的以太网数据（非阻塞）。
 * @param buf 输出缓冲。
 * @param max_len 缓冲容量。
 * @param out_len 实际帧长度。
 * @return 0 表示取到一帧，-1 表示暂无数据。
 */
int usb_ecm_frame_read(uint8_t *buf, uint16_t max_len, uint16_t *out_len);

/**
 * @brief 通过 ECM 数据端点发送一帧以太网数据（同步等待完成）。
 * @param data 帧数据。
 * @param length 帧长度。
 * @return 0 表示已发出，-1 表示失败。
 */
int usb_ecm_send(const uint8_t *data, uint16_t length);

/**
 * @brief 设备层接收回调（由 usbd_ecm_if.c 在中断中调用）。
 * @param data 帧数据。
 * @param len 帧长度。
 * @return 无。
 */
void rp_usb_ecm_on_rx(const uint8_t *data, uint32_t len);

/**
 * @brief USB 设备是否已完成配置（枚举成功）。
 * @return 1 表示已配置，0 表示未配置。
 */
int usb_ecm_is_configured(void);

#ifdef __cplusplus
}
#endif

#endif /* _rp_device_usb_ecm_h_ */
