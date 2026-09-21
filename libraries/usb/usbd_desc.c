#include "usbd_core.h"
#include "usbd_ctlreq.h"
#include "usbd_desc.h"
#include "usbd_conf.h"

/** @brief USB 厂商/产品 ID。
 *  VID 保持 ST 的 0x0483；PID 用本项目自定义值 0x5250（避开 ST 示例的 0x5740）。 */
#define USBD_VID                      0x0483U
#define USBD_PID                      0x5250U
#define USBD_LANGID_STRING            1033U
#define USBD_MANUFACTURER_STRING      "USB-ECM"
#define USBD_PRODUCT_STRING_FS        "STM32 USB Ethernet"
#define USBD_CONFIGURATION_STRING_FS  "ECM Config"
#define USBD_INTERFACE_STRING_FS      "ECM Interface"

/** @brief 序列号字符串描述符总长度。 */
#define USB_SIZ_STRING_SERIAL         0x1AU
/** @brief 通用字符串描述符缓冲。 */
#define USB_STRING_BUF_SIZE           64U

static void IntToUnicode(uint32_t value, uint8_t *pbuf, uint8_t len);
static void Get_SerialNum(void);

uint8_t *USBD_FS_DeviceDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
uint8_t *USBD_FS_LangIDStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
uint8_t *USBD_FS_ManufacturerStrDescriptor(USBD_SpeedTypeDef speed,
                                           uint16_t *length);
uint8_t *USBD_FS_ProductStrDescriptor(USBD_SpeedTypeDef speed,
                                      uint16_t *length);
uint8_t *USBD_FS_SerialStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
uint8_t *USBD_FS_ConfigStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
uint8_t *USBD_FS_InterfaceStrDescriptor(USBD_SpeedTypeDef speed,
                                        uint16_t *length);

USBD_DescriptorsTypeDef FS_Desc = {
  USBD_FS_DeviceDescriptor,
  USBD_FS_LangIDStrDescriptor,
  USBD_FS_ManufacturerStrDescriptor,
  USBD_FS_ProductStrDescriptor,
  USBD_FS_SerialStrDescriptor,
  USBD_FS_ConfigStrDescriptor,
  USBD_FS_InterfaceStrDescriptor
};

static const uint8_t s_device_desc[USB_LEN_DEV_DESC] = {
  0x12U, USB_DESC_TYPE_DEVICE, 0x00U, 0x02U, 0xEFU, 0x02U, 0x01U, 0x40U,
  (uint8_t)(USBD_VID & 0xFFU), (uint8_t)(USBD_VID >> 8U),
  (uint8_t)(USBD_PID & 0xFFU), (uint8_t)(USBD_PID >> 8U),
  0x00U, 0x02U, 0x01U, 0x02U, 0x03U, 0x01U
};

static const uint8_t s_langid_desc[USB_LEN_LANGID_STR_DESC] = {
  USB_LEN_LANGID_STR_DESC, USB_DESC_TYPE_STRING,
  (uint8_t)(USBD_LANGID_STRING & 0xFFU), (uint8_t)(USBD_LANGID_STRING >> 8U)
};

static uint8_t s_str_buf[USB_STRING_BUF_SIZE];
static uint8_t s_serial_desc[USB_SIZ_STRING_SERIAL] = {0U};
static uint8_t s_serial_ready = 0U;

uint8_t *USBD_FS_DeviceDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  (void)speed;
  *length = USB_LEN_DEV_DESC;
  return (uint8_t *)s_device_desc;
}

uint8_t *USBD_FS_LangIDStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  (void)speed;
  *length = USB_LEN_LANGID_STR_DESC;
  return (uint8_t *)s_langid_desc;
}

/**
 * @brief 把 ASCII 串转换为 USB 字符串描述符。
 */
static uint8_t *MakeStringDesc(const char *str, uint16_t *length)
{
  (void)USBD_GetString((uint8_t *)str, s_str_buf, length);
  return s_str_buf;
}

uint8_t *USBD_FS_ManufacturerStrDescriptor(USBD_SpeedTypeDef speed,
                                           uint16_t *length)
{
  (void)speed;
  return MakeStringDesc(USBD_MANUFACTURER_STRING, length);
}

uint8_t *USBD_FS_ProductStrDescriptor(USBD_SpeedTypeDef speed,
                                      uint16_t *length)
{
  (void)speed;
  return MakeStringDesc(USBD_PRODUCT_STRING_FS, length);
}

uint8_t *USBD_FS_ConfigStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  (void)speed;
  return MakeStringDesc(USBD_CONFIGURATION_STRING_FS, length);
}

uint8_t *USBD_FS_InterfaceStrDescriptor(USBD_SpeedTypeDef speed,
                                        uint16_t *length)
{
  (void)speed;
  return MakeStringDesc(USBD_INTERFACE_STRING_FS, length);
}

uint8_t *USBD_FS_SerialStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  (void)speed;
  *length = USB_SIZ_STRING_SERIAL;
  if (s_serial_ready == 0U) {
    Get_SerialNum();
    s_serial_ready = 1U;
  }
  return s_serial_desc;
}

/**
 * @brief 将 32 位值按 8 位一组转成 Unicode(UTF-16LE) 十六进制字符。
 * @param value 待转换值。
 * @param pbuf 输出缓冲，长度至少 2*len 字节。
 * @param len 要转换的十六进制字符数。
 * @return 无。
 */
static void IntToUnicode(uint32_t value, uint8_t *pbuf, uint8_t len)
{
  uint8_t idx;

  for (idx = 0U; idx < len; ++idx) {
    uint8_t nibble = (uint8_t)((value >> 28) & 0x0FU);
    pbuf[2U * idx] = (nibble < 10U) ? (uint8_t)(nibble + '0')
                                    : (uint8_t)(nibble + 'A' - 10U);
    value <<= 4U;
    pbuf[2U * idx + 1U] = 0U;
  }
}

/**
 * @brief 用芯片 UID 生成 USB 序列号字符串描述符。
 */
static void Get_SerialNum(void)
{
  uint32_t uid0 = HAL_GetUIDw0();
  uint32_t uid1 = HAL_GetUIDw1();
  uint32_t uid2 = HAL_GetUIDw2();

  uid0 += uid2;
  if (uid0 != 0U) {
    s_serial_desc[0] = USB_SIZ_STRING_SERIAL;
    s_serial_desc[1] = USB_DESC_TYPE_STRING;
    IntToUnicode(uid0, &s_serial_desc[2], 8U);
    IntToUnicode(uid1, &s_serial_desc[18], 4U);
  }
}
