#include "rp_device_usb_ecm.h"

#include <string.h>

#include "usbd_core.h"
#include "usbd_desc.h"
#include "usbd_ecm_if.h"

/** @brief ECM 设备句柄。 */
USBD_HandleTypeDef hUsbDeviceEcm;
/** @brief PCD 句柄（定义在 usbd_conf.c）。 */
extern PCD_HandleTypeDef hpcd_USB_FS;

/** @brief 接收帧环形缓冲槽数。 */
#define USB_ECM_RX_FRAMES  4U

static uint8_t s_frame[USB_ECM_RX_FRAMES][ECM_MAX_FRAME_SIZE];
static volatile uint16_t s_frame_len[USB_ECM_RX_FRAMES];
static volatile uint8_t s_head;
static volatile uint8_t s_tail;
/** @brief 链路通知阶段：0 未发，1 已发速率，2 已发连接。 */
static uint8_t s_notify_stage;
/** @brief 上次发送连接通知的时刻（用于周期性重发）。 */
static uint32_t s_last_notify_ms;
/** @brief 连接通知重发间隔（ms）。 */
#define USB_ECM_NOTIFY_PERIOD_MS  3000U

void rp_usb_ecm_on_rx(const uint8_t *data, uint32_t len)
{
  uint8_t head;
  uint8_t next;
  uint16_t copy_len;

  if ((data == NULL) || (len == 0U)) {
    return;
  }

  head = s_head;
  next = (uint8_t)((head + 1U) % USB_ECM_RX_FRAMES);
  if (next == s_tail) {
    return; /* 环形缓冲满，丢弃该帧 */
  }

  copy_len = (len > ECM_MAX_FRAME_SIZE) ? (uint16_t)ECM_MAX_FRAME_SIZE
                                        : (uint16_t)len;
  (void)memcpy(s_frame[head], data, copy_len);
  s_frame_len[head] = copy_len;
  s_head = next;
}

int usb_ecm_frame_read(uint8_t *buf, uint16_t max_len, uint16_t *out_len)
{
  uint8_t tail;
  uint16_t len;

  if ((buf == NULL) || (out_len == NULL) || (s_tail == s_head)) {
    return -1;
  }

  tail = s_tail;
  len = s_frame_len[tail];
  if (len > max_len) {
    len = max_len;
  }
  (void)memcpy(buf, s_frame[tail], len);
  *out_len = len;
  s_tail = (uint8_t)((tail + 1U) % USB_ECM_RX_FRAMES);
  return 0;
}

int usb_ecm_send(const uint8_t *data, uint16_t length)
{
  if ((data == NULL) || (length == 0U)) {
    return -1;
  }
  return (ECM_Transmit_FS((uint8_t *)data, length) == (uint8_t)USBD_OK) ? 0 : -1;
}

rp_status_t usb_ecm_init(void)
{
  s_head = 0U;
  s_tail = 0U;
  s_notify_stage = 0U;
  s_last_notify_ms = 0U;

  if (USBD_Init(&hUsbDeviceEcm, &FS_Desc, DEVICE_FS) != USBD_OK) {
    return RP_ERROR;
  }
  if (USBD_RegisterClass(&hUsbDeviceEcm, &USBD_ECM) != USBD_OK) {
    return RP_ERROR;
  }
  if (USBD_ECM_RegisterInterface(&hUsbDeviceEcm, &USBD_Interface_fops_ECM) !=
      USBD_OK) {
    return RP_ERROR;
  }
  if (USBD_Start(&hUsbDeviceEcm) != USBD_OK) {
    return RP_ERROR;
  }
  return RP_OK;
}

void usb_ecm_poll(void)
{
  static const uint8_t speed[8] = {
    0x00U, 0x1BU, 0xB7U, 0x00U, /* 上游 12 Mbps */
    0x00U, 0x1BU, 0xB7U, 0x00U  /* 下游 12 Mbps */
  };
  uint32_t now;

  if (hUsbDeviceEcm.dev_state != USBD_STATE_CONFIGURED) {
    s_notify_stage = 0U;
    return;
  }

  now = HAL_GetTick();

  if (s_notify_stage == 0U) {
    if (USBD_ECM_SendNotification(&hUsbDeviceEcm,
                                  ECM_NOTIFY_CONNECTION_SPEED_CHANGE,
                                  speed, sizeof(speed)) == (uint8_t)USBD_OK) {
      s_notify_stage = 1U;
    }
    return;
  }

  if (s_notify_stage == 1U) {
    if (USBD_ECM_SendNotification(&hUsbDeviceEcm,
                                  ECM_NOTIFY_NETWORK_CONNECTION,
                                  NULL, 0U) == (uint8_t)USBD_OK) {
      s_notify_stage = 2U;
      s_last_notify_ms = now;
    }
    return;
  }

  /* 驱动可能在通知之后才启动，周期性重发“已连接”以确保链路被判为 up。 */
  if ((now - s_last_notify_ms) >= USB_ECM_NOTIFY_PERIOD_MS) {
    if (USBD_ECM_SendNotification(&hUsbDeviceEcm,
                                  ECM_NOTIFY_NETWORK_CONNECTION,
                                  NULL, 0U) == (uint8_t)USBD_OK) {
      s_last_notify_ms = now;
    }
  }
}

void usb_ecm_irq_handler(void)
{
  HAL_PCD_IRQHandler(&hpcd_USB_FS);
}

int usb_ecm_is_configured(void)
{
  return (hUsbDeviceEcm.dev_state == USBD_STATE_CONFIGURED) ? 1 : 0;
}
