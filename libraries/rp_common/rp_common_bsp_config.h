#ifndef _rp_common_bsp_config_h_
#define _rp_common_bsp_config_h_

#include "stm32g4xx_hal.h"

/**
 * @file rp_common_bsp_config.h
 * @brief BSP 层集中默认参数（STM32 USB-ECM 工程）。
 *
 * 原理图无法确定的数值集中在本文件，并带有 TODO 标注。
 * 应用层可在调用 BSP_xxx_Init() 时传入自定义配置覆盖这些默认值。
 */

/* ------------------------------- 按键 --------------------------------- */
#define KEY_ACTIVE_LOW          1U /* TODO: 确认按下电平。 */

/* ------------------------------- UART --------------------------------- */
#define UART_DEFAULT_BAUDRATE    115200U /* 调试串口波特率。 */
#define UART_RX_BUFFER_SIZE      128U    /* 每路 UART 接收环形缓冲大小。 */
#define UART_IRQ_PREEMPT_PRIORITY 1U
#define UART_IRQ_SUB_PRIORITY    0U

/* ------------------------------- SPI ---------------------------------- */
#define SPI_DEFAULT_PRESCALER   SPI_BAUDRATEPRESCALER_2 /* /2 = 85MHz（PCLK2=170MHz）。 */
#define SPI_DEFAULT_CPOL        SPI_POLARITY_LOW        /* ST7789 mode0。 */
#define SPI_DEFAULT_CPHA        SPI_PHASE_1EDGE

/* ------------------------------- PIT ---------------------------------- */
#define PIT_DEFAULT_PERIOD_MS    10U /* 周期任务节拍，单位 ms。 */
#define PIT_IRQ_PREEMPT_PRIORITY 0U
#define PIT_IRQ_SUB_PRIORITY     0U

/* ----------------------------- 看门狗 --------------------------------- */
#define IWDG_DEFAULT_TIMEOUT_MS  1000U /* 独立看门狗默认超时，单位 ms。 */

/* --------------------------- 模块使能开关 --------------------------- */
#define BSP_ENABLE_KEY      1U /* PA4~PA7 */
#define BSP_ENABLE_UART1    0U /* 调试串口由 debug_init() 自行初始化 */
#define BSP_ENABLE_UART2    0U
#define BSP_ENABLE_UART3    0U
#define BSP_ENABLE_SPI1     1U /* LCD 依赖 */
#define BSP_ENABLE_PIT      1U
#if defined(DEBUG)
#define BSP_ENABLE_IWDG     0U /* 调试时关闭，避免断点触发复位 */
#else
#define BSP_ENABLE_IWDG     1U
#endif

/* ------------------------------- LCD ---------------------------------- */
/* 逻辑分辨率：当前为横屏（面板 240x280 顺时针旋转 90°）。 */
#define LCD_WIDTH           280U
#define LCD_HEIGHT          240U
#define LCD_X_OFFSET        0U  /* CASET（列）偏移。 */
#define LCD_Y_OFFSET        20U /* RASET（行）偏移：面板在 240x320 控制器内起始行。 */
/* ST7789 MADCTL(0x36)：0x00 竖屏，0x60=MV|MX / 0xA0=MV|MY 为两种横屏（相差 180°），
 * 0xC0=MX|MY 竖屏180°。当前使用 0xA0 横屏。 */
#define LCD_MADCTL          0xA0U
#define LCD_TIMEOUT_MS      1000U
#define LCD_CS_PORT         GPIOD
#define LCD_CS_PIN          GPIO_PIN_11
#define LCD_DC_PIN          GPIO_PIN_12
#define LCD_BL_PIN          GPIO_PIN_13
#define LCD_BL_ACTIVE_HIGH  1U /* 背光高电平点亮 */

#endif /* _rp_common_bsp_config_h_ */
