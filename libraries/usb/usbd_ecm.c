/**
 * @file usbd_ecm.c
 * @brief USB CDC-ECM 设备类实现（基于 ST 经典 USB Device 栈）。
 */
#include "usbd_ecm.h"

#include <string.h>

#include "usbd_conf.h"
#include "usbd_ctlreq.h"

/* 私有函数声明 ------------------------------------------------------------- */
static uint8_t USBD_ECM_Init(USBD_HandleTypeDef *pdev, uint8_t cfgidx);
static uint8_t USBD_ECM_DeInit(USBD_HandleTypeDef *pdev, uint8_t cfgidx);
static uint8_t USBD_ECM_Setup(USBD_HandleTypeDef *pdev,
                              USBD_SetupReqTypedef *req);
static uint8_t USBD_ECM_DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum);
static uint8_t USBD_ECM_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum);
static uint8_t USBD_ECM_EP0_RxReady(USBD_HandleTypeDef *pdev);

static uint8_t *USBD_ECM_GetFSCfgDesc(uint16_t *length);
static uint8_t *USBD_ECM_GetHSCfgDesc(uint16_t *length);
static uint8_t *USBD_ECM_GetOtherSpeedCfgDesc(uint16_t *length);
static uint8_t *USBD_ECM_GetDeviceQualifierDescriptor(uint16_t *length);
static uint8_t *USBD_ECM_GetUsrStrDescriptor(USBD_HandleTypeDef *pdev,
                                             uint8_t index, uint16_t *length);

/* 设备限定符描述符 --------------------------------------------------------- */
__ALIGN_BEGIN static uint8_t USBD_ECM_DeviceQualifierDesc[USB_LEN_DEV_QUALIFIER_DESC] __ALIGN_END =
{
  USB_LEN_DEV_QUALIFIER_DESC,
  USB_DESC_TYPE_DEVICE_QUALIFIER,
  0x00,
  0x02,
  0x00,
  0x00,
  0x00,
  0x40,
  0x01,
  0x00,
};

/* 类配置描述符 ------------------------------------------------------------- */
__ALIGN_BEGIN static uint8_t USBD_ECM_CfgDesc[USB_ECM_CONFIG_DESC_SIZ] __ALIGN_END =
{
  /* Configuration Descriptor */
  0x09,                                /* bLength */
  USB_DESC_TYPE_CONFIGURATION,         /* bDescriptorType */
  USB_ECM_CONFIG_DESC_SIZ,             /* wTotalLength (L) */
  0x00,                                /* wTotalLength (H) */
  0x02,                                /* bNumInterfaces: 2 */
  0x01,                                /* bConfigurationValue */
  0x00,                                /* iConfiguration */
  0xC0,                                /* bmAttributes: self powered */
  0x32,                                /* bMaxPower = 100 mA */

  /* Interface Association Descriptor (CDC) */
  0x08,                                /* bLength */
  USB_DESC_TYPE_IAD,                   /* bDescriptorType */
  0x00,                                /* bFirstInterface */
  0x02,                                /* bInterfaceCount */
  0x02,                                /* bFunctionClass: CDC */
  0x06,                                /* bFunctionSubClass: ECM */
  0x00,                                /* bFunctionProtocol */
  0x00,                                /* iFunction */

  /* Interface 0: Communication Class (ECM) */
  0x09,                                /* bLength */
  USB_DESC_TYPE_INTERFACE,             /* bDescriptorType */
  0x00,                                /* bInterfaceNumber */
  0x00,                                /* bAlternateSetting */
  0x01,                                /* bNumEndpoints: 1 (notification) */
  0x02,                                /* bInterfaceClass: CDC */
  0x06,                                /* bInterfaceSubClass: ECM */
  0x00,                                /* bInterfaceProtocol */
  0x00,                                /* iInterface */

  /* CDC Header Functional Descriptor */
  0x05,                                /* bLength */
  ECM_DESC_TYPE_CS_INTERFACE,          /* bDescriptorType: CS_INTERFACE */
  ECM_DESC_SUBTYPE_HEADER,             /* bDescriptorSubtype: Header */
  0x20,                                /* bcdCDC (L) = 1.20 */
  0x01,                                /* bcdCDC (H) */

  /* CDC Union Functional Descriptor */
  0x05,                                /* bLength */
  ECM_DESC_TYPE_CS_INTERFACE,          /* bDescriptorType: CS_INTERFACE */
  ECM_DESC_SUBTYPE_UNION,              /* bDescriptorSubtype: Union */
  0x00,                                /* bControlInterface = 0 */
  0x01,                                /* bSubordinateInterface0 = 1 */

  /* Ethernet Networking Functional Descriptor (13 字节) */
  ECM_ETH_FUNC_DESC_SIZE,              /* bLength = 13 */
  ECM_DESC_TYPE_CS_INTERFACE,          /* bDescriptorType: CS_INTERFACE */
  ECM_DESC_SUBTYPE_ECM,                /* bDescriptorSubtype: Ethernet Networking */
  ECM_MAC_STRING_INDEX,                /* iMACAddress */
  0x00, 0x00, 0x00, 0x00,              /* bmEthernetStatistics */
  (uint8_t)(ECM_WMAXSEGMENT_SIZE & 0xFFU), /* wMaxSegmentSize (L) */
  (uint8_t)(ECM_WMAXSEGMENT_SIZE >> 8),    /* wMaxSegmentSize (H) */
  0x00, 0x00,                          /* wNumberMCFilters = 0 */
  0x00,                                /* bNumberPowerFilters = 0 */

  /* Notification Endpoint Descriptor (interrupt IN) */
  0x07,                                /* bLength */
  USB_DESC_TYPE_ENDPOINT,              /* bDescriptorType */
  ECM_NOTIFY_EP,                       /* bEndpointAddress */
  0x03,                                /* bmAttributes: interrupt */
  ECM_NOTIFY_PACKET_SIZE, 0x00,        /* wMaxPacketSize */
  ECM_FS_BINTERVAL,                    /* bInterval */

  /* Interface 1: Data Class, alternate 0（空闲，无端点） */
  0x09,                                /* bLength */
  USB_DESC_TYPE_INTERFACE,             /* bDescriptorType */
  0x01,                                /* bInterfaceNumber */
  0x00,                                /* bAlternateSetting */
  0x00,                                /* bNumEndpoints: 0 */
  0x0A,                                /* bInterfaceClass: CDC Data */
  0x00,                                /* bInterfaceSubClass */
  0x00,                                /* bInterfaceProtocol */
  0x00,                                /* iInterface */

  /* Interface 1: Data Class, alternate 1（激活，含批量端点） */
  0x09,                                /* bLength */
  USB_DESC_TYPE_INTERFACE,             /* bDescriptorType */
  0x01,                                /* bInterfaceNumber */
  0x01,                                /* bAlternateSetting */
  0x02,                                /* bNumEndpoints: 2 */
  0x0A,                                /* bInterfaceClass: CDC Data */
  0x00,                                /* bInterfaceSubClass */
  0x00,                                /* bInterfaceProtocol */
  0x00,                                /* iInterface */

  /* Data IN Endpoint (bulk) */
  0x07,                                /* bLength */
  USB_DESC_TYPE_ENDPOINT,              /* bDescriptorType */
  ECM_DATA_IN_EP,                      /* bEndpointAddress */
  0x02,                                /* bmAttributes: bulk */
  ECM_DATA_FS_MAX_PACKET_SIZE, 0x00,   /* wMaxPacketSize */
  0x00,                                /* bInterval */

  /* Data OUT Endpoint (bulk) */
  0x07,                                /* bLength */
  USB_DESC_TYPE_ENDPOINT,              /* bDescriptorType */
  ECM_DATA_OUT_EP,                     /* bEndpointAddress */
  0x02,                                /* bmAttributes: bulk */
  ECM_DATA_FS_MAX_PACKET_SIZE, 0x00,   /* wMaxPacketSize */
  0x00,                                /* bInterval */
};

_Static_assert(sizeof(USBD_ECM_CfgDesc) == USB_ECM_CONFIG_DESC_SIZ,
               "ECM config descriptor size mismatch");

/** @brief ECM 类回调表。 */
USBD_ClassTypeDef USBD_ECM =
{
  USBD_ECM_Init,
  USBD_ECM_DeInit,
  USBD_ECM_Setup,
  NULL,                       /* EP0_TxSent */
  USBD_ECM_EP0_RxReady,
  USBD_ECM_DataIn,
  USBD_ECM_DataOut,
  NULL,                       /* SOF */
  NULL,                       /* IsoINIncomplete */
  NULL,                       /* IsoOUTIncomplete */
  USBD_ECM_GetHSCfgDesc,
  USBD_ECM_GetFSCfgDesc,
  USBD_ECM_GetOtherSpeedCfgDesc,
  USBD_ECM_GetDeviceQualifierDescriptor,
#if (USBD_SUPPORT_USER_STRING_DESC == 1U)
  USBD_ECM_GetUsrStrDescriptor,
#endif /* USBD_SUPPORT_USER_STRING_DESC */
};

/** @brief Notify 端点当前是否忙。 */
static __IO uint8_t s_notify_busy;

static USBD_ECM_HandleTypeDef *ecm_handle(USBD_HandleTypeDef *pdev)
{
  return (USBD_ECM_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];
}

static int8_t ecm_itf_control(USBD_HandleTypeDef *pdev, uint8_t cmd,
                              uint8_t *pbuf, uint16_t length)
{
  USBD_ECM_ItfTypeDef *itf =
      (USBD_ECM_ItfTypeDef *)pdev->pUserData[pdev->classId];

  if ((itf == NULL) || (itf->Control == NULL)) {
    return (int8_t)USBD_OK;
  }
  return itf->Control(cmd, pbuf, length);
}

/**
 * @brief 激活数据接口（alternate 1）：打开批量端点并准备接收。
 */
static uint8_t ecm_open_data(USBD_HandleTypeDef *pdev)
{
  USBD_ECM_HandleTypeDef *hEcm = ecm_handle(pdev);

  if ((hEcm == NULL) || (hEcm->RxBuffer == NULL)) {
    return (uint8_t)USBD_FAIL;
  }
  if (hEcm->data_alt == ECM_DATA_ALT_ACTIVE) {
    return (uint8_t)USBD_OK;
  }

  (void)USBD_LL_OpenEP(pdev, ECM_DATA_IN_EP, USBD_EP_TYPE_BULK,
                       ECM_DATA_FS_MAX_PACKET_SIZE);
  pdev->ep_in[ECM_DATA_IN_EP & 0x0FU].is_used = 1U;

  (void)USBD_LL_OpenEP(pdev, ECM_DATA_OUT_EP, USBD_EP_TYPE_BULK,
                       ECM_DATA_FS_MAX_PACKET_SIZE);
  pdev->ep_out[ECM_DATA_OUT_EP & 0x0FU].is_used = 1U;

  hEcm->TxState = 0U;
  hEcm->RxState = 0U;
  (void)USBD_LL_PrepareReceive(pdev, ECM_DATA_OUT_EP, hEcm->RxBuffer,
                               ECM_RX_ARM_SIZE);
  hEcm->data_alt = ECM_DATA_ALT_ACTIVE;
  return (uint8_t)USBD_OK;
}

/**
 * @brief 关闭数据接口（切回 alternate 0）。
 */
static uint8_t ecm_close_data(USBD_HandleTypeDef *pdev)
{
  USBD_ECM_HandleTypeDef *hEcm = ecm_handle(pdev);

  if (hEcm == NULL) {
    return (uint8_t)USBD_FAIL;
  }
  if (hEcm->data_alt == ECM_DATA_ALT_IDLE) {
    return (uint8_t)USBD_OK;
  }

  (void)USBD_LL_CloseEP(pdev, ECM_DATA_IN_EP);
  pdev->ep_in[ECM_DATA_IN_EP & 0x0FU].is_used = 0U;

  (void)USBD_LL_CloseEP(pdev, ECM_DATA_OUT_EP);
  pdev->ep_out[ECM_DATA_OUT_EP & 0x0FU].is_used = 0U;

  hEcm->TxState = 0U;
  hEcm->RxState = 0U;
  hEcm->data_alt = ECM_DATA_ALT_IDLE;
  return (uint8_t)USBD_OK;
}

/**
 * @brief 打开配置：分配句柄、打开通知端点；数据端点在 SET_INTERFACE 时打开。
 */
static uint8_t USBD_ECM_Init(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
  USBD_ECM_HandleTypeDef *hEcm;
  USBD_ECM_ItfTypeDef *itf =
      (USBD_ECM_ItfTypeDef *)pdev->pUserData[pdev->classId];

  (void)cfgidx;

  hEcm = (USBD_ECM_HandleTypeDef *)USBD_malloc(sizeof(USBD_ECM_HandleTypeDef));
  if (hEcm == NULL) {
    pdev->pClassDataCmsit[pdev->classId] = NULL;
    return (uint8_t)USBD_EMEM;
  }
  (void)USBD_memset(hEcm, 0, sizeof(USBD_ECM_HandleTypeDef));
  pdev->pClassDataCmsit[pdev->classId] = (void *)hEcm;
  pdev->pClassData = pdev->pClassDataCmsit[pdev->classId];
  s_notify_busy = 0U;

  /* 通知端点（中断 IN，位于控制接口）。 */
  (void)USBD_LL_OpenEP(pdev, ECM_NOTIFY_EP, USBD_EP_TYPE_INTR,
                       ECM_NOTIFY_PACKET_SIZE);
  pdev->ep_in[ECM_NOTIFY_EP & 0x0FU].is_used = 1U;
  pdev->ep_in[ECM_NOTIFY_EP & 0x0FU].bInterval = ECM_FS_BINTERVAL;

  hEcm->RxBuffer = NULL;
  hEcm->RxState = 0U;
  hEcm->packet_filter = ECM_DEFAULT_PACKET_FILTER;
  hEcm->link_up = 0U;
  hEcm->data_alt = ECM_DATA_ALT_IDLE;

  if ((itf != NULL) && (itf->Init != NULL)) {
    (void)itf->Init();
  }
  return (uint8_t)USBD_OK;
}

/**
 * @brief 关闭配置：关端点、释放句柄。
 */
static uint8_t USBD_ECM_DeInit(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
  USBD_ECM_ItfTypeDef *itf =
      (USBD_ECM_ItfTypeDef *)pdev->pUserData[pdev->classId];

  (void)cfgidx;

  (void)ecm_close_data(pdev);

  (void)USBD_LL_CloseEP(pdev, ECM_NOTIFY_EP);
  pdev->ep_in[ECM_NOTIFY_EP & 0x0FU].is_used = 0U;
  pdev->ep_in[ECM_NOTIFY_EP & 0x0FU].bInterval = 0U;

  if (pdev->pClassDataCmsit[pdev->classId] != NULL) {
    if ((itf != NULL) && (itf->DeInit != NULL)) {
      (void)itf->DeInit();
    }
    (void)USBD_free(pdev->pClassDataCmsit[pdev->classId]);
    pdev->pClassDataCmsit[pdev->classId] = NULL;
    pdev->pClassData = NULL;
  }

  return (uint8_t)USBD_OK;
}

/**
 * @brief 处理类/标准请求。
 */
static uint8_t USBD_ECM_Setup(USBD_HandleTypeDef *pdev,
                              USBD_SetupReqTypedef *req)
{
  USBD_ECM_HandleTypeDef *hEcm = ecm_handle(pdev);
  uint16_t status_info = 0U;
  uint8_t ifalt = 0U;
  USBD_StatusTypeDef ret = USBD_OK;

  if (hEcm == NULL) {
    return (uint8_t)USBD_FAIL;
  }

  switch (req->bmRequest & USB_REQ_TYPE_MASK)
  {
    case USB_REQ_TYPE_CLASS:
      switch (req->bRequest)
      {
        case ECM_SET_ETHERNET_PACKET_FILTER:
          hEcm->packet_filter = req->wValue;
          (void)ecm_itf_control(pdev, req->bRequest, (uint8_t *)req, 0U);
          break;

        case ECM_SET_ETHERNET_MULTICAST_FILTERS:
          if (req->wLength != 0U) {
            (void)USBD_CtlPrepareRx(pdev, hEcm->data,
                                    MIN(req->wLength, ECM_DATA_FS_MAX_PACKET_SIZE));
          }
          break;

        case ECM_GET_ETHERNET_PMP_FILTER:
          (void)USBD_CtlSendData(pdev, hEcm->data, MIN(req->wLength, 2U));
          break;

        case ECM_SET_ETHERNET_PMP_FILTER:
          if (req->wLength != 0U) {
            (void)USBD_CtlPrepareRx(pdev, hEcm->data,
                                    MIN(req->wLength, ECM_DATA_FS_MAX_PACKET_SIZE));
          }
          break;

        case ECM_GET_ETHERNET_STATISTIC:
          (void)USBD_memset(hEcm->data, 0, 4U);
          (void)USBD_CtlSendData(pdev, hEcm->data, MIN(req->wLength, 4U));
          break;

        default:
          (void)ecm_itf_control(pdev, req->bRequest, (uint8_t *)req, 0U);
          break;
      }
      break;

    case USB_REQ_TYPE_STANDARD:
      switch (req->bRequest)
      {
        case USB_REQ_GET_STATUS:
          if (pdev->dev_state == USBD_STATE_CONFIGURED) {
            (void)USBD_CtlSendData(pdev, (uint8_t *)&status_info, 2U);
          } else {
            USBD_CtlError(pdev, req);
            ret = USBD_FAIL;
          }
          break;

        case USB_REQ_GET_INTERFACE:
          if (pdev->dev_state == USBD_STATE_CONFIGURED) {
            ifalt = (req->wIndex == ECM_DATA_ITF) ? hEcm->data_alt : 0U;
            (void)USBD_CtlSendData(pdev, &ifalt, 1U);
          } else {
            USBD_CtlError(pdev, req);
            ret = USBD_FAIL;
          }
          break;

        case USB_REQ_SET_INTERFACE:
          if (pdev->dev_state != USBD_STATE_CONFIGURED) {
            USBD_CtlError(pdev, req);
            ret = USBD_FAIL;
          } else {
            if (req->wIndex == ECM_DATA_ITF) {
              if (req->wValue == ECM_DATA_ALT_ACTIVE) {
                (void)ecm_open_data(pdev);
              } else {
                (void)ecm_close_data(pdev);
              }
            }
            /* 状态阶段由 USB 核心在 wLength==0 时统一发送。 */
          }
          break;

        case USB_REQ_GET_DESCRIPTOR:
          /* 主机的类描述符请求：返回以太网功能描述符。 */
          if (((req->wValue >> 8) & 0xFFU) == ECM_DESC_TYPE_CS_INTERFACE) {
            static const uint8_t eth_func[ECM_ETH_FUNC_DESC_SIZE] = {
              ECM_ETH_FUNC_DESC_SIZE, ECM_DESC_TYPE_CS_INTERFACE,
              ECM_DESC_SUBTYPE_ECM,
              ECM_MAC_STRING_INDEX,
              0x00, 0x00, 0x00, 0x00,
              (uint8_t)(ECM_WMAXSEGMENT_SIZE & 0xFFU),
              (uint8_t)(ECM_WMAXSEGMENT_SIZE >> 8),
              0x00, 0x00,
              0x00
            };
            (void)USBD_CtlSendData(pdev, (uint8_t *)eth_func,
                                   MIN(req->wLength, ECM_ETH_FUNC_DESC_SIZE));
          } else {
            USBD_CtlError(pdev, req);
            ret = USBD_FAIL;
          }
          break;

        case USB_REQ_CLEAR_FEATURE:
          break;

        default:
          USBD_CtlError(pdev, req);
          ret = USBD_FAIL;
          break;
      }
      break;

    default:
      USBD_CtlError(pdev, req);
      ret = USBD_FAIL;
      break;
  }

  return (uint8_t)ret;
}

/**
 * @brief 非控制 IN 端点发送完成。
 */
static uint8_t USBD_ECM_DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
  USBD_ECM_HandleTypeDef *hEcm = ecm_handle(pdev);
  PCD_HandleTypeDef *hpcd = (PCD_HandleTypeDef *)pdev->pData;
  USBD_ECM_ItfTypeDef *itf;

  if (hEcm == NULL) {
    return (uint8_t)USBD_FAIL;
  }

  if ((epnum & 0x7FU) == (ECM_NOTIFY_EP & 0x7FU)) {
    s_notify_busy = 0U;
    return (uint8_t)USBD_OK;
  }

  if ((pdev->ep_in[epnum & 0x0FU].total_length > 0U) &&
      ((pdev->ep_in[epnum & 0x0FU].total_length %
        hpcd->IN_ep[epnum & 0x0FU].maxpacket) == 0U)) {
    pdev->ep_in[epnum & 0x0FU].total_length = 0U;
    (void)USBD_LL_Transmit(pdev, epnum, NULL, 0U);
  } else {
    hEcm->TxState = 0U;
    itf = (USBD_ECM_ItfTypeDef *)pdev->pUserData[pdev->classId];
    if ((itf != NULL) && (itf->TransmitCplt != NULL)) {
      (void)itf->TransmitCplt(hEcm->TxBuffer, &hEcm->TxLength, epnum);
    }
  }

  return (uint8_t)USBD_OK;
}

/**
 * @brief 数据 OUT 端点收到一帧。
 */
static uint8_t USBD_ECM_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
  USBD_ECM_HandleTypeDef *hEcm = ecm_handle(pdev);
  USBD_ECM_ItfTypeDef *itf;

  if (hEcm == NULL) {
    return (uint8_t)USBD_FAIL;
  }

  hEcm->RxLength = USBD_LL_GetRxDataSize(pdev, epnum);
  itf = (USBD_ECM_ItfTypeDef *)pdev->pUserData[pdev->classId];
  if ((itf != NULL) && (itf->Receive != NULL)) {
    (void)itf->Receive(hEcm->RxBuffer, hEcm->RxLength);
  }
  return (uint8_t)USBD_OK;
}

/**
 * @brief 控制 OUT 数据接收完成（忽略多播过滤等负载）。
 */
static uint8_t USBD_ECM_EP0_RxReady(USBD_HandleTypeDef *pdev)
{
  (void)pdev;
  return (uint8_t)USBD_OK;
}

static uint8_t *USBD_ECM_GetFSCfgDesc(uint16_t *length)
{
  *length = (uint16_t)sizeof(USBD_ECM_CfgDesc);
  return USBD_ECM_CfgDesc;
}

static uint8_t *USBD_ECM_GetHSCfgDesc(uint16_t *length)
{
  *length = (uint16_t)sizeof(USBD_ECM_CfgDesc);
  return USBD_ECM_CfgDesc;
}

static uint8_t *USBD_ECM_GetOtherSpeedCfgDesc(uint16_t *length)
{
  *length = (uint16_t)sizeof(USBD_ECM_CfgDesc);
  return USBD_ECM_CfgDesc;
}

static uint8_t *USBD_ECM_GetDeviceQualifierDescriptor(uint16_t *length)
{
  *length = (uint16_t)sizeof(USBD_ECM_DeviceQualifierDesc);
  return USBD_ECM_DeviceQualifierDesc;
}

/**
 * @brief 类私有字符串描述符：返回 iMACAddress 指向的 MAC 字符串。
 * @note 由 UID 派生，与网卡 MAC 一致（02:00:xx:xx:xx:xx）。
 */
static uint8_t *USBD_ECM_GetUsrStrDescriptor(USBD_HandleTypeDef *pdev,
                                             uint8_t index, uint16_t *length)
{
  static uint8_t s_mac_str[2U + 12U * 2U];
  static uint8_t s_mac_ready;
  static const char hex[] = "0123456789ABCDEF";

  (void)pdev;

  if (index != ECM_MAC_STRING_INDEX) {
    *length = 0U;
    return NULL;
  }

  if (s_mac_ready == 0U) {
    uint32_t uid = HAL_GetUIDw0() ^ HAL_GetUIDw1() ^ HAL_GetUIDw2();
    uint8_t mac[6];
    uint32_t i;

    mac[0] = 0x02U;
    mac[1] = 0x00U;
    mac[2] = (uint8_t)(uid >> 24);
    mac[3] = (uint8_t)(uid >> 16);
    mac[4] = (uint8_t)(uid >> 8);
    mac[5] = (uint8_t)uid;

    s_mac_str[0] = (uint8_t)sizeof(s_mac_str);
    s_mac_str[1] = USB_DESC_TYPE_STRING;
    for (i = 0U; i < 6U; ++i) {
      s_mac_str[2U + i * 4U] = (uint8_t)hex[mac[i] >> 4];
      s_mac_str[3U + i * 4U] = 0U;
      s_mac_str[4U + i * 4U] = (uint8_t)hex[mac[i] & 0x0FU];
      s_mac_str[5U + i * 4U] = 0U;
    }
    s_mac_ready = 1U;
  }

  *length = s_mac_str[0];
  return s_mac_str;
}

/* 应用接口 ---------------------------------------------------------------- */

uint8_t USBD_ECM_RegisterInterface(USBD_HandleTypeDef *pdev,
                                   USBD_ECM_ItfTypeDef *fops)
{
  if (fops == NULL) {
    return (uint8_t)USBD_FAIL;
  }
  pdev->pUserData[pdev->classId] = fops;
  return (uint8_t)USBD_OK;
}

uint8_t USBD_ECM_SetTxBuffer(USBD_HandleTypeDef *pdev, uint8_t *pbuff,
                             uint32_t length)
{
  USBD_ECM_HandleTypeDef *hEcm = ecm_handle(pdev);

  if (hEcm == NULL) {
    return (uint8_t)USBD_FAIL;
  }
  hEcm->TxBuffer = pbuff;
  hEcm->TxLength = length;
  return (uint8_t)USBD_OK;
}

uint8_t USBD_ECM_SetRxBuffer(USBD_HandleTypeDef *pdev, uint8_t *pbuff)
{
  USBD_ECM_HandleTypeDef *hEcm = ecm_handle(pdev);

  if (hEcm == NULL) {
    return (uint8_t)USBD_FAIL;
  }
  hEcm->RxBuffer = pbuff;
  return (uint8_t)USBD_OK;
}

uint8_t USBD_ECM_TransmitPacket(USBD_HandleTypeDef *pdev)
{
  USBD_ECM_HandleTypeDef *hEcm = ecm_handle(pdev);

  if (hEcm == NULL) {
    return (uint8_t)USBD_FAIL;
  }
  if (hEcm->TxState == 0U) {
    hEcm->TxState = 1U;
    (void)USBD_LL_Transmit(pdev, ECM_DATA_IN_EP, hEcm->TxBuffer,
                           hEcm->TxLength);
  }
  return (uint8_t)USBD_OK;
}

uint8_t USBD_ECM_ReceivePacket(USBD_HandleTypeDef *pdev)
{
  USBD_ECM_HandleTypeDef *hEcm = ecm_handle(pdev);

  if (hEcm == NULL) {
    return (uint8_t)USBD_FAIL;
  }
  (void)USBD_LL_PrepareReceive(pdev, ECM_DATA_OUT_EP, hEcm->RxBuffer,
                               ECM_RX_ARM_SIZE);
  return (uint8_t)USBD_OK;
}

uint8_t USBD_ECM_SendNotification(USBD_HandleTypeDef *pdev, uint8_t type,
                                  const uint8_t *data, uint16_t len)
{
  USBD_ECM_HandleTypeDef *hEcm = ecm_handle(pdev);
  uint16_t wValue = (type == ECM_NOTIFY_NETWORK_CONNECTION) ? 1U : 0U;

  if (hEcm == NULL) {
    return (uint8_t)USBD_FAIL;
  }
  if (s_notify_busy != 0U) {
    return (uint8_t)USBD_BUSY;
  }
  if ((uint16_t)(8U + len) > ECM_NOTIFY_PACKET_SIZE) {
    return (uint8_t)USBD_FAIL;
  }

  hEcm->notify_buf[0] = 0xA1U;                 /* bmRequestType */
  hEcm->notify_buf[1] = type;                  /* bNotificationType */
  hEcm->notify_buf[2] = (uint8_t)(wValue & 0xFFU);
  hEcm->notify_buf[3] = (uint8_t)(wValue >> 8);
  hEcm->notify_buf[4] = 0x00U;                 /* wIndex = interface 0 */
  hEcm->notify_buf[5] = 0x00U;
  hEcm->notify_buf[6] = (uint8_t)(len & 0xFFU);
  hEcm->notify_buf[7] = (uint8_t)(len >> 8);
  if ((data != NULL) && (len != 0U)) {
    (void)USBD_memcpy(&hEcm->notify_buf[8], data, len);
  }

  s_notify_busy = 1U;
  (void)USBD_LL_Transmit(pdev, ECM_NOTIFY_EP, hEcm->notify_buf,
                         (uint16_t)(8U + len));
  return (uint8_t)USBD_OK;
}
