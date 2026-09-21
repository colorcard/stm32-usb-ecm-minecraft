/**
 * @file usbd_ecm_if.c
 * @brief ECM 类接口回调实现：连接 ECM 类与设备层收发。
 */
#include "usbd_ecm_if.h"

#include "usbd_conf.h"

/** @brief 设备实例（定义于 rp_device_usb_ecm.c）。 */
extern USBD_HandleTypeDef hUsbDeviceEcm;
/** @brief 设备层接收回调（定义于 rp_device_usb_ecm.c）。 */
extern void rp_usb_ecm_on_rx(const uint8_t *data, uint32_t len);

/** @brief 发送等待超时。 */
#define ECM_TX_TIMEOUT_MS 200U

/** @brief 接收缓冲：需容纳整帧。 */
static uint8_t s_rx_buf[ECM_RX_ARM_SIZE];

static int8_t ECM_Init_FS(void);
static int8_t ECM_DeInit_FS(void);
static int8_t ECM_Control_FS(uint8_t cmd, uint8_t *pbuf, uint16_t length);
static int8_t ECM_Receive_FS(uint8_t *Buf, uint32_t Len);
static int8_t ECM_TransmitCplt_FS(uint8_t *Buf, uint32_t *Len, uint8_t epnum);

USBD_ECM_ItfTypeDef USBD_Interface_fops_ECM = {
  ECM_Init_FS,
  ECM_DeInit_FS,
  ECM_Control_FS,
  ECM_Receive_FS,
  ECM_TransmitCplt_FS
};

static int8_t ECM_Init_FS(void)
{
  (void)USBD_ECM_SetRxBuffer(&hUsbDeviceEcm, s_rx_buf);
  return (int8_t)USBD_OK;
}

static int8_t ECM_DeInit_FS(void)
{
  return (int8_t)USBD_OK;
}

static int8_t ECM_Control_FS(uint8_t cmd, uint8_t *pbuf, uint16_t length)
{
  (void)cmd;
  (void)pbuf;
  (void)length;
  return (int8_t)USBD_OK;
}

static int8_t ECM_Receive_FS(uint8_t *Buf, uint32_t Len)
{
  rp_usb_ecm_on_rx(Buf, Len);
  (void)USBD_ECM_ReceivePacket(&hUsbDeviceEcm);
  return (int8_t)USBD_OK;
}

static int8_t ECM_TransmitCplt_FS(uint8_t *Buf, uint32_t *Len, uint8_t epnum)
{
  (void)Buf;
  (void)Len;
  (void)epnum;
  return (int8_t)USBD_OK;
}

uint8_t ECM_Transmit_FS(uint8_t *Buf, uint16_t Len)
{
  USBD_ECM_HandleTypeDef *hEcm =
      (USBD_ECM_HandleTypeDef *)hUsbDeviceEcm.pClassData;
  uint32_t start;

  if (hEcm == NULL) {
    return (uint8_t)USBD_FAIL;
  }

  start = HAL_GetTick();
  while (hEcm->TxState != 0U) {
    if ((HAL_GetTick() - start) > ECM_TX_TIMEOUT_MS) {
      return (uint8_t)USBD_BUSY;
    }
  }

  (void)USBD_ECM_SetTxBuffer(&hUsbDeviceEcm, Buf, Len);
  if (USBD_ECM_TransmitPacket(&hUsbDeviceEcm) != (uint8_t)USBD_OK) {
    return (uint8_t)USBD_FAIL;
  }

  while (hEcm->TxState != 0U) {
    if ((HAL_GetTick() - start) > ECM_TX_TIMEOUT_MS) {
      return (uint8_t)USBD_BUSY;
    }
  }
  return (uint8_t)USBD_OK;
}
