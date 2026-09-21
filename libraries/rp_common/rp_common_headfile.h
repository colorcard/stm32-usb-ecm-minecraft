#ifndef _rp_common_headfile_h_
#define _rp_common_headfile_h_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ========================= 公共层 ========================= */
#include "rp_common_bsp_config.h"
#include "rp_common_bsp.h"
#include "rp_common_clock.h"
#include "rp_common_debug.h"
#include "rp_common_delay.h"
#include "rp_common_fault.h"
#include "rp_common_fifo.h"
#include "rp_common_interrupt.h"

/* ====================== 芯片外设驱动层 ====================== */
#include "rp_driver_gpio.h"
#include "rp_driver_pit.h"
#include "rp_driver_spi.h"
#include "rp_driver_timer.h"
#include "rp_driver_uart.h"
#include "rp_driver_watchdog.h"

/* ====================== 外接设备驱动层 ====================== */
#include "rp_device_key.h"
#include "rp_device_lcd_hw.h"

#endif /* _rp_common_headfile_h_ */
