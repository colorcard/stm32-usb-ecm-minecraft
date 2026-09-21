#include "rp_common_headfile.h"
#include "rp_common_fault.h"
#include "rp_device_usb_ecm.h"

#include "FreeRTOS.h"
#include "task.h"

extern void xPortSysTickHandler(void);

/******************************************************************************/
/*                       Cortex-M4 处理器异常处理                              */
/******************************************************************************/

void NMI_Handler(void)
{
  while (1) {
  }
}

/**
 * @brief 故障统一 C 处理入口。
 * @param stack_frame 异常压栈的栈帧首地址（r0,r1,r2,r3,r12,lr,pc,xpsr）。
 * @param exc_return 进入异常时的 LR（EXC_RETURN）。
 * @return 无，捕获现场后停在 while (1) 中等待看门狗复位。
 * @note 由 HardFault/MemManage/BusFault/UsageFault 的 naked 处理函数跳转进入；
 *       非 static 全局函数以确保内联汇编中的 b 指令可见。
 */
void fault_handler_c(uint32_t *stack_frame, uint32_t exc_return)
{
  fault_capture(stack_frame, exc_return);
  fault_hook();
  while (1) {
  }
}

__attribute__((naked)) void HardFault_Handler(void)
{
  __asm volatile(
    "tst lr, #4        \n"
    "ite eq            \n"
    "mrseq r0, msp     \n"
    "mrsne r0, psp     \n"
    "mov r1, lr        \n"
    "b fault_handler_c \n");
}

__attribute__((naked)) void MemManage_Handler(void)
{
  __asm volatile(
    "tst lr, #4        \n"
    "ite eq            \n"
    "mrseq r0, msp     \n"
    "mrsne r0, psp     \n"
    "mov r1, lr        \n"
    "b fault_handler_c \n");
}

__attribute__((naked)) void BusFault_Handler(void)
{
  __asm volatile(
    "tst lr, #4        \n"
    "ite eq            \n"
    "mrseq r0, msp     \n"
    "mrsne r0, psp     \n"
    "mov r1, lr        \n"
    "b fault_handler_c \n");
}

__attribute__((naked)) void UsageFault_Handler(void)
{
  __asm volatile(
    "tst lr, #4        \n"
    "ite eq            \n"
    "mrseq r0, msp     \n"
    "mrsne r0, psp     \n"
    "mov r1, lr        \n"
    "b fault_handler_c \n");
}

/* SVC_Handler / PendSV_Handler 由 FreeRTOS 移植提供（见 FreeRTOSConfig.h 向量映射）。 */
void DebugMon_Handler(void)
{
}

/**
 * @brief SysTick：调度器未启动时仅维护 HAL 毫秒计数；启动后驱动 FreeRTOS tick。
 */
void SysTick_Handler(void)
{
  HAL_IncTick();
  if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
    xPortSysTickHandler();
  }
}

/******************************************************************************/
/*                          外设中断服务函数                                    */
/******************************************************************************/

/**
 * @brief TIM1 触发/换相中断与 TIM17 全局中断（共用向量）。
 * @return 无。
 */
void TIM1_TRG_COM_TIM17_IRQHandler(void)
{
  TIM_HandleTypeDef *tim1 = timer_get_handle(TIMER_1);
  TIM_HandleTypeDef *tim17 = timer_get_handle(TIMER_17);

  if ((tim1 != NULL) && (tim1->Instance != NULL)) {
    HAL_TIM_IRQHandler(tim1);
  }
  if ((tim17 != NULL) && (tim17->Instance != NULL)) {
    HAL_TIM_IRQHandler(tim17);
  }
}

/**
 * @brief DMA1 通道 3 中断：SPI1 TX（LCD 刷屏）DMA 完成。
 * @return 无。
 */
void DMA1_Channel3_IRQHandler(void)
{
  DMA_HandleTypeDef *hdma = spi_dma_tx_handle(SPI_1);

  if (hdma != NULL) {
    HAL_DMA_IRQHandler(hdma);
  }
}

/**
 * @brief USB 低优先级中断：USB Device 事件。
 * @return 无。
 */
void USB_LP_IRQHandler(void)
{
  usb_ecm_irq_handler();
}

/**
 * @brief 定时器周期完成回调，用于 10 ms 按键扫描。
 * @param htim 触发周期完成事件的定时器句柄。
 * @return 无。
 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  (void)htim;
}

/**
 * @brief USART1 全局中断，用于接收缓冲。
 * @return 无。
 */
void USART1_IRQHandler(void)
{
  HAL_UART_IRQHandler(uart_get_handle(UART_1));
}

/**
 * @brief USART2 全局中断，用于接收缓冲。
 * @return 无。
 */
void USART2_IRQHandler(void)
{
  HAL_UART_IRQHandler(uart_get_handle(UART_2));
}

/**
 * @brief USART3 全局中断，用于接收缓冲。
 * @return 无。
 */
void USART3_IRQHandler(void)
{
  HAL_UART_IRQHandler(uart_get_handle(UART_3));
}
