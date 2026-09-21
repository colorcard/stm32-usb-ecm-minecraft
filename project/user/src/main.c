#include "rp_common_headfile.h"
#include "rp_device_usb_ecm.h"
#include "rp_lwip.h"
#include "UCraft.h"

#include "FreeRTOS.h"
#include "task.h"

/** @brief UCraft 运行标志（本移植不使用 CLI，故不会触发退出）。 */
static uint8_t s_ucraft_cleanup;

/** @brief net 任务心跳（调试）。 */
volatile uint32_t net_poll_count;

/** @brief 网络任务：驱动 USB-ECM 收帧并喂给 lwIP。 */
static void task_net(void *arg)
{
  (void)arg;
  for (;;) {
    rp_lwip_poll();
    net_poll_count++;
#if (BSP_ENABLE_IWDG != 0U)
    iwdg_feed();
#endif
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

/** @brief 调试：UCraft 任务是否进入、UCraftStart 返回时的阶段。 */
volatile int uc_dbg_task_entered;
volatile int uc_dbg_ucraft_returned;

/** @brief UCraft 服务端任务：阻塞在其自身的 socket 循环中。 */
static void task_ucraft(void *arg)
{
  (void)arg;
  uc_dbg_task_entered = 1;
  (void)UCraftStart(&s_ucraft_cleanup);
  uc_dbg_ucraft_returned = 1;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

/**
 * @brief 应用入口。
 * @return 不会返回。
 */
int main(void)
{
  (*(volatile uint32_t *)0xE000E008UL) |= (1UL << 1U);

  HAL_Init();
  clock_init();
  if (bsp_init() != RP_OK) {
    error_handler();
  }

  debug_init();
  (void)usb_ecm_init();
  (void)rp_lwip_init();

  (void)xTaskCreate(task_net, "net", 512U, NULL, 2U, NULL);
  (void)xTaskCreate(task_ucraft, "ucraft", 2048U, NULL, 3U, NULL);

  vTaskStartScheduler();

  while (1) {
  }
}

/** @brief FreeRTOS 内存分配失败钩子。 */
void vApplicationMallocFailedHook(void)
{
  error_handler();
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
  (void)file;
  (void)line;
  while (1) {
  }
}
#endif
