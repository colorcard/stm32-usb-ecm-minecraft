#include "rp_common_headfile.h"
#include "rp_device_usb_ecm.h"
#include "rp_lwip.h"
#include "server_display.h"
#include "UCraft.h"

/** @brief UCraft 运行标志（本移植不使用 CLI，故不会触发退出）。 */
static uint8_t s_ucraft_cleanup;

/**
 * @brief 应用入口。
 * @return 不会返回。
 * @note 初始化顺序：HAL -> 时钟 -> 板级外设 -> USB ECM -> lwIP -> 显示
 *       -> UCraft 服务端（阻塞运行）。
 */
int main(void)
{
  /* 让总线错误精确上报，便于定位非法访问。 */
  (*(volatile uint32_t *)0xE000E008UL) |= (1UL << 1U);

  HAL_Init();
  clock_init();
  if (bsp_init() != RP_OK) {
    error_handler();
  }

  debug_init();
  (void)lcd_hw_init();
  (void)usb_ecm_init();
  (void)rp_lwip_init();
  server_display_init();

  (void)UCraftStart(&s_ucraft_cleanup);

  while (1) {
  }
}

#ifdef USE_FULL_ASSERT
/**
 * @brief 参数断言失败处理。
 * @param file 源文件名。
 * @param line 出错行号。
 * @return 无。
 */
void assert_failed(uint8_t *file, uint32_t line)
{
  (void)file;
  (void)line;
  while (1) {
  }
}
#endif
