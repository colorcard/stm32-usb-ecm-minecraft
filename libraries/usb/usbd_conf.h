#ifndef __USBD_CONF_H
#define __USBD_CONF_H

#include <stdint.h>
#include <string.h>

#include "stm32g4xx_hal.h"

#define USBD_MAX_NUM_INTERFACES     2U
#define USBD_MAX_NUM_CONFIGURATION  1U
#define USBD_MAX_STR_DESC_SIZ       512U
#define USBD_DEBUG_LEVEL            0U
#define USBD_SELF_POWERED           1U

#define DEVICE_FS                   0U
#define DEVICE_HS                   1U

void *USBD_static_malloc(uint32_t size);
void USBD_static_free(void *p);

#define USBD_malloc   (void *)USBD_static_malloc
#define USBD_free     USBD_static_free
#define USBD_memset   memset
#define USBD_memcpy   memcpy
#define USBD_Delay    HAL_Delay

#endif /* __USBD_CONF_H */
