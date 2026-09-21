#include "usbd_core.h"
#include "usbd_desc.h"
#include "usbd_ecm.h"
#include "usbd_conf.h"

/** @brief PA11/PA12 的 USB FS 复用功能号（AF10）。 */
#define USB_FS_AF       0x0AU
/** @brief USB 设备栈静态内存池（避免动态分配）。 */
#define USBD_POOL_SIZE  1536U

/** @brief 全局 PCD 句柄。 */
PCD_HandleTypeDef hpcd_USB_FS;

static uint8_t s_usbd_pool[USBD_POOL_SIZE];
static uint32_t s_usbd_pool_used;

extern void error_handler(void);

void *USBD_static_malloc(uint32_t size)
{
  /* 4 字节对齐分配，池满返回 NULL。 */
  uint32_t aligned = (size + 3U) & ~3U;
  void *ptr = NULL;

  if ((s_usbd_pool_used + aligned) <= USBD_POOL_SIZE) {
    ptr = &s_usbd_pool[s_usbd_pool_used];
    s_usbd_pool_used += aligned;
  }
  return ptr;
}

void USBD_static_free(void *p)
{
  (void)p;
}

void HAL_PCD_MspInit(PCD_HandleTypeDef *pcdHandle)
{
  GPIO_InitTypeDef gpio = {0};
  RCC_PeriphCLKInitTypeDef clk = {0};
  RCC_OscInitTypeDef osc = {0};
  RCC_CRSInitTypeDef crs = {0};

  if (pcdHandle->Instance != USB) {
    return;
  }

  __HAL_RCC_USB_CLK_ENABLE();

  /* USB 时钟：HSI48，并用 CRS 锁定到 USB SOF。 */
  clk.PeriphClockSelection = RCC_PERIPHCLK_USB;
  clk.UsbClockSelection = RCC_USBCLKSOURCE_HSI48;
  if (HAL_RCCEx_PeriphCLKConfig(&clk) != HAL_OK) {
    error_handler();
  }
  osc.OscillatorType = RCC_OSCILLATORTYPE_HSI48;
  osc.HSI48State = RCC_HSI48_ON;
  if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
    error_handler();
  }
  __HAL_RCC_CRS_CLK_ENABLE();
  crs.Prescaler = RCC_CRS_SYNC_DIV1;
  crs.Source = RCC_CRS_SYNC_SOURCE_USB;
  crs.Polarity = RCC_CRS_SYNC_POLARITY_RISING;
  crs.ReloadValue = __HAL_RCC_CRS_RELOADVALUE_CALCULATE(48000000U, 1000U);
  crs.ErrorLimitValue = RCC_CRS_ERRORLIMIT_DEFAULT;
  crs.HSI48CalibrationValue = RCC_CRS_HSI48CALIBRATION_DEFAULT;
  HAL_RCCEx_CRSConfig(&crs);

  /* PA11 = USB_DM，PA12 = USB_DP（AF10）。 */
  __HAL_RCC_GPIOA_CLK_ENABLE();
  gpio.Pin = GPIO_PIN_11 | GPIO_PIN_12;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  gpio.Alternate = USB_FS_AF;
  HAL_GPIO_Init(GPIOA, &gpio);

  HAL_NVIC_SetPriority(USB_LP_IRQn, 1U, 0U);
  HAL_NVIC_EnableIRQ(USB_LP_IRQn);
}

void HAL_PCD_MspDeInit(PCD_HandleTypeDef *pcdHandle)
{
  if (pcdHandle->Instance == USB) {
    __HAL_RCC_USB_CLK_DISABLE();
    HAL_NVIC_DisableIRQ(USB_LP_IRQn);
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_11 | GPIO_PIN_12);
  }
}

/* ------------------------------ LL 接口 ------------------------------ */

USBD_StatusTypeDef USBD_LL_Init(USBD_HandleTypeDef *pdev)
{
  hpcd_USB_FS.Instance = USB;
  hpcd_USB_FS.Init.dev_endpoints = 8U;
  hpcd_USB_FS.Init.speed = PCD_SPEED_FULL;
  hpcd_USB_FS.Init.phy_itface = PCD_PHY_EMBEDDED;
  hpcd_USB_FS.Init.Sof_enable = DISABLE;
  hpcd_USB_FS.Init.low_power_enable = DISABLE;
  hpcd_USB_FS.Init.lpm_enable = DISABLE;
  hpcd_USB_FS.Init.battery_charging_enable = DISABLE;
  hpcd_USB_FS.Init.ep0_mps = 0x40U;
  if (HAL_PCD_Init(&hpcd_USB_FS) != HAL_OK) {
    return USBD_FAIL;
  }

  pdev->pData = &hpcd_USB_FS;
  hpcd_USB_FS.pData = pdev;

  /* PMA 缓冲分配（端点 0 双向，ECM 通知/数据）。 */
  HAL_PCDEx_PMAConfig(pdev->pData, 0x00U, PCD_SNG_BUF, 0x18U);
  HAL_PCDEx_PMAConfig(pdev->pData, 0x80U, PCD_SNG_BUF, 0x58U);
  HAL_PCDEx_PMAConfig(pdev->pData, ECM_DATA_IN_EP, PCD_SNG_BUF, 0x98U);
  HAL_PCDEx_PMAConfig(pdev->pData, ECM_DATA_OUT_EP, PCD_SNG_BUF, 0xD8U);
  HAL_PCDEx_PMAConfig(pdev->pData, ECM_NOTIFY_EP, PCD_SNG_BUF, 0x118U);
  return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_DeInit(USBD_HandleTypeDef *pdev)
{
  (void)HAL_PCD_DeInit(pdev->pData);
  return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_Start(USBD_HandleTypeDef *pdev)
{
  (void)HAL_PCD_Start(pdev->pData);
  return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_Stop(USBD_HandleTypeDef *pdev)
{
  (void)HAL_PCD_Stop(pdev->pData);
  return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_OpenEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr,
                                  uint8_t ep_type, uint16_t ep_mps)
{
  (void)HAL_PCD_EP_Open(pdev->pData, ep_addr, ep_mps, ep_type);
  return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_CloseEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
  (void)HAL_PCD_EP_Close(pdev->pData, ep_addr);
  return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_FlushEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
  (void)HAL_PCD_EP_Flush(pdev->pData, ep_addr);
  return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_StallEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
  (void)HAL_PCD_EP_SetStall(pdev->pData, ep_addr);
  return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_ClearStallEP(USBD_HandleTypeDef *pdev,
                                        uint8_t ep_addr)
{
  (void)HAL_PCD_EP_ClrStall(pdev->pData, ep_addr);
  return USBD_OK;
}

uint8_t USBD_LL_IsStallEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
  PCD_HandleTypeDef *hpcd = (PCD_HandleTypeDef *)pdev->pData;

  if ((ep_addr & 0x80U) != 0U) {
    return hpcd->IN_ep[ep_addr & 0x7FU].is_stall;
  }
  return hpcd->OUT_ep[ep_addr & 0x7FU].is_stall;
}

USBD_StatusTypeDef USBD_LL_SetUSBAddress(USBD_HandleTypeDef *pdev,
                                         uint8_t dev_addr)
{
  (void)HAL_PCD_SetAddress(pdev->pData, dev_addr);
  return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_Transmit(USBD_HandleTypeDef *pdev, uint8_t ep_addr,
                                    uint8_t *pbuf, uint32_t size)
{
  (void)HAL_PCD_EP_Transmit(pdev->pData, ep_addr, pbuf, size);
  return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_PrepareReceive(USBD_HandleTypeDef *pdev,
                                          uint8_t ep_addr, uint8_t *pbuf,
                                          uint32_t size)
{
  (void)HAL_PCD_EP_Receive(pdev->pData, ep_addr, pbuf, size);
  return USBD_OK;
}

uint32_t USBD_LL_GetRxDataSize(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
  return HAL_PCD_EP_GetRxCount(pdev->pData, ep_addr);
}

void USBD_LL_Delay(uint32_t Delay)
{
  HAL_Delay(Delay);
}

/* --------------------------- PCD 事件回调 --------------------------- */

void HAL_PCD_SetupStageCallback(PCD_HandleTypeDef *hpcd)
{
  USBD_LL_SetupStage((USBD_HandleTypeDef *)hpcd->pData, (uint8_t *)hpcd->Setup);
}

void HAL_PCD_DataOutStageCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
{
  USBD_LL_DataOutStage((USBD_HandleTypeDef *)hpcd->pData, epnum,
                       hpcd->OUT_ep[epnum].xfer_buff);
}

void HAL_PCD_DataInStageCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
{
  USBD_LL_DataInStage((USBD_HandleTypeDef *)hpcd->pData, epnum,
                      hpcd->IN_ep[epnum].xfer_buff);
}

void HAL_PCD_SOFCallback(PCD_HandleTypeDef *hpcd)
{
  USBD_LL_SOF((USBD_HandleTypeDef *)hpcd->pData);
}

void HAL_PCD_ResetCallback(PCD_HandleTypeDef *hpcd)
{
  USBD_LL_SetSpeed((USBD_HandleTypeDef *)hpcd->pData, USBD_SPEED_FULL);
  USBD_LL_Reset((USBD_HandleTypeDef *)hpcd->pData);
}

void HAL_PCD_SuspendCallback(PCD_HandleTypeDef *hpcd)
{
  USBD_LL_Suspend((USBD_HandleTypeDef *)hpcd->pData);
}

void HAL_PCD_ResumeCallback(PCD_HandleTypeDef *hpcd)
{
  USBD_LL_Resume((USBD_HandleTypeDef *)hpcd->pData);
}

void HAL_PCD_ISOOUTIncompleteCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
{
  USBD_LL_IsoOUTIncomplete((USBD_HandleTypeDef *)hpcd->pData, epnum);
}

void HAL_PCD_ISOINIncompleteCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
{
  USBD_LL_IsoINIncomplete((USBD_HandleTypeDef *)hpcd->pData, epnum);
}

void HAL_PCD_ConnectCallback(PCD_HandleTypeDef *hpcd)
{
  USBD_LL_DevConnected((USBD_HandleTypeDef *)hpcd->pData);
}

void HAL_PCD_DisconnectCallback(PCD_HandleTypeDef *hpcd)
{
  USBD_LL_DevDisconnected((USBD_HandleTypeDef *)hpcd->pData);
}
