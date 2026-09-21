/**
 * @file usbd_ecm.h
 * @brief USB CDC-ECM（Ethernet Networking Control Model）设备类。
 *
 * 在 ST 经典 USB Device 栈上实现 CDC-ECM：对主机呈现为一张免驱以太网卡
 * （macOS/Linux 原生支持）。数据接口以标准以太网帧收发。
 */
#ifndef __USB_ECM_H
#define __USB_ECM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "usbd_ioreq.h"

/** @defgroup ECM 端点定义 */
/** @{ */
#define ECM_NOTIFY_EP                0x82U /**< 中断 IN：状态通知。 */
#define ECM_DATA_IN_EP               0x81U /**< 批量 IN：设备->主机帧。 */
#define ECM_DATA_OUT_EP              0x01U /**< 批量 OUT：主机->设备帧。 */
/** @} */

/** @brief 通知端点包大小。 */
#define ECM_NOTIFY_PACKET_SIZE       16U
/** @brief 单个以太网帧最大长度（含 14 字节头，不含 FCS）。 */
#ifndef ECM_MAX_FRAME_SIZE
#define ECM_MAX_FRAME_SIZE           1536U
#endif
/** @brief 数据 OUT 端点武装长度：整帧一次收完（HAL 多包累积）。 */
#ifndef ECM_RX_ARM_SIZE
#define ECM_RX_ARM_SIZE              ECM_MAX_FRAME_SIZE
#endif
/** @brief 全速批量端点包大小。 */
#define ECM_DATA_FS_MAX_PACKET_SIZE  64U
/** @brief 高速批量端点包大小（本设备仅全速，保留定义）。 */
#define ECM_DATA_HS_MAX_PACKET_SIZE  512U
/** @brief 通知端点轮询间隔。 */
#define ECM_FS_BINTERVAL             16U

/** @brief 配置描述符总长度（含 IAD、数据接口双 alternate）。 */
#define USB_ECM_CONFIG_DESC_SIZ      88U

/** @brief 通信（控制）接口号。 */
#define ECM_COMM_ITF                 0U
/** @brief 数据接口号。 */
#define ECM_DATA_ITF                 1U
/** @brief 数据接口空闲 alternate（无端点）。 */
#define ECM_DATA_ALT_IDLE            0U
/** @brief 数据接口激活 alternate（含批量端点）。 */
#define ECM_DATA_ALT_ACTIVE          1U

/** @brief CS_INTERFACE 描述符类型。 */
#define ECM_DESC_TYPE_CS_INTERFACE   0x24U
/** @brief Header 功能描述符子类型。 */
#define ECM_DESC_SUBTYPE_HEADER      0x00U
/** @brief Union 功能描述符子类型。 */
#define ECM_DESC_SUBTYPE_UNION       0x06U
/** @brief Ethernet Networking 功能描述符子类型。 */
#define ECM_DESC_SUBTYPE_ECM         0x0FU
/** @brief Ethernet Networking 功能描述符长度（ECM 规范为 13）。 */
#define ECM_ETH_FUNC_DESC_SIZE       13U
/** @brief 最大以太网段长度（含 14 字节头，不含 FCS）。 */
#define ECM_WMAXSEGMENT_SIZE         1514U
/** @brief iMACAddress 字符串描述符索引（类私有，避开标准 1..5）。 */
#define ECM_MAC_STRING_INDEX         6U

/** @defgroup ECM 类请求码（CDC 1.2 第 6.3 节） */
/** @{ */
#define ECM_SET_ETHERNET_MULTICAST_FILTERS  0x40U
#define ECM_SET_ETHERNET_PMP_FILTER         0x41U
#define ECM_GET_ETHERNET_PMP_FILTER         0x42U
#define ECM_SET_ETHERNET_PACKET_FILTER      0x43U
#define ECM_GET_ETHERNET_STATISTIC          0x44U
/** @} */

/** @defgroup ECM 通知码 */
/** @{ */
#define ECM_NOTIFY_NETWORK_CONNECTION       0x00U
#define ECM_NOTIFY_RESPONSE_AVAILABLE       0x01U
#define ECM_NOTIFY_CONNECTION_SPEED_CHANGE  0x2AU
/** @} */

/** @defgroup ECM 包过滤位 */
/** @{ */
#define ECM_PACKET_TYPE_PROMISCUOUS         0x0001U
#define ECM_PACKET_TYPE_ALL_MULTICAST       0x0002U
#define ECM_PACKET_TYPE_DIRECTED            0x0004U
#define ECM_PACKET_TYPE_BROADCAST           0x0008U
#define ECM_PACKET_TYPE_MULTICAST           0x0010U
#define ECM_DEFAULT_PACKET_FILTER \
  (ECM_PACKET_TYPE_DIRECTED | ECM_PACKET_TYPE_BROADCAST | ECM_PACKET_TYPE_MULTICAST)
/** @} */

/**
 * @brief ECM 应用层回调。
 */
typedef struct _USBD_ECM_Itf
{
  int8_t (*Init)(void);       /**< 接口初始化。 */
  int8_t (*DeInit)(void);     /**< 接口反初始化。 */
  int8_t (*Control)(uint8_t cmd, uint8_t *pbuf, uint16_t length); /**< 类请求。 */
  int8_t (*Receive)(uint8_t *Buf, uint32_t Len); /**< 收到一帧。 */
  int8_t (*TransmitCplt)(uint8_t *Buf, uint32_t *Len, uint8_t epnum); /**< 发送完成。 */
} USBD_ECM_ItfTypeDef;

/**
 * @brief ECM 类运行时数据。
 */
typedef struct
{
  uint8_t  data[ECM_DATA_FS_MAX_PACKET_SIZE]; /**< 类请求数据缓冲。 */
  uint8_t  notify_buf[ECM_NOTIFY_PACKET_SIZE]; /**< 通知缓冲。 */
  uint8_t  *RxBuffer;         /**< 当前接收缓冲。 */
  uint32_t RxLength;          /**< 最近接收长度。 */
  uint8_t  *TxBuffer;         /**< 当前发送缓冲。 */
  uint32_t TxLength;          /**< 发送长度。 */
  __IO uint32_t TxState;      /**< 发送状态。 */
  __IO uint32_t RxState;      /**< 接收状态。 */
  uint16_t packet_filter;     /**< 当前包过滤设置。 */
  uint8_t  link_up;           /**< 链路是否已通知主机。 */
  uint8_t  data_alt;          /**< 数据接口当前 alternate。 */
} USBD_ECM_HandleTypeDef;

/** @brief USBD ECM 类对象（注册到设备栈）。 */
extern USBD_ClassTypeDef USBD_ECM;

uint8_t USBD_ECM_RegisterInterface(USBD_HandleTypeDef *pdev,
                                   USBD_ECM_ItfTypeDef *fops);
uint8_t USBD_ECM_SetTxBuffer(USBD_HandleTypeDef *pdev, uint8_t *pbuff,
                             uint32_t length);
uint8_t USBD_ECM_SetRxBuffer(USBD_HandleTypeDef *pdev, uint8_t *pbuff);
uint8_t USBD_ECM_TransmitPacket(USBD_HandleTypeDef *pdev);
uint8_t USBD_ECM_ReceivePacket(USBD_HandleTypeDef *pdev);

/**
 * @brief 向主机发送 ECM 状态通知。
 * @param pdev 设备实例。
 * @param type 通知类型（ECM_NOTIFY_*）。
 * @param data 附加数据，可为 NULL。
 * @param len  附加数据长度。
 * @return USBD_OK / USBD_FAIL / USBD_BUSY。
 */
uint8_t USBD_ECM_SendNotification(USBD_HandleTypeDef *pdev, uint8_t type,
                                  const uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* __USB_ECM_H */
