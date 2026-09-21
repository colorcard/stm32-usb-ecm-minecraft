/**
 * @file sys_arch.c
 * @brief lwIP 在 FreeRTOS 上的 sys_arch 实现（NO_SYS=0）。
 */
#include "lwip/opt.h"

#if !NO_SYS

#include "lwip/sys.h"
#include "lwip/err.h"

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"

void sys_init(void)
{
}

/** @brief lwIP 毫秒时间基准（FreeRTOS tick 1ms）。 */
u32_t sys_now(void)
{
  return (u32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

void sys_arch_msleep(uint32_t ms)
{
  vTaskDelay(pdMS_TO_TICKS(ms));
}

/* ------------------------------ 信号量 ------------------------------ */
err_t sys_sem_new(sys_sem_t *sem, u8_t count)
{
  if (sem == NULL) {
    return ERR_ARG;
  }
  *sem = xSemaphoreCreateCounting(0xFFFFU, count);
  return (*sem == NULL) ? ERR_MEM : ERR_OK;
}

void sys_sem_free(sys_sem_t *sem)
{
  if ((sem != NULL) && (*sem != SYS_SEM_NULL)) {
    vSemaphoreDelete(*sem);
    *sem = SYS_SEM_NULL;
  }
}

void sys_sem_signal(sys_sem_t *sem)
{
  if ((sem != NULL) && (*sem != SYS_SEM_NULL)) {
    (void)xSemaphoreGive(*sem);
  }
}

u32_t sys_arch_sem_wait(sys_sem_t *sem, u32_t timeout)
{
  TickType_t ticks;
  TimeOut_t to;

  if ((sem == NULL) || (*sem == SYS_SEM_NULL)) {
    return SYS_ARCH_TIMEOUT;
  }
  if (timeout == 0U) {
    if (xSemaphoreTake(*sem, portMAX_DELAY) == pdTRUE) {
      return 0U;
    }
    return SYS_ARCH_TIMEOUT;
  }
  ticks = pdMS_TO_TICKS(timeout);
  vTaskSetTimeOutState(&to);
  if (xSemaphoreTake(*sem, ticks) == pdTRUE) {
    uint32_t elapsed = (uint32_t)(xTaskGetTickCount() - to.xTimeOnEntering);
    return (u32_t)(elapsed * portTICK_PERIOD_MS);
  }
  return SYS_ARCH_TIMEOUT;
}

/* ------------------------------ 互斥量 ------------------------------ */
err_t sys_mutex_new(sys_mutex_t *mutex)
{
  if (mutex == NULL) {
    return ERR_ARG;
  }
  *mutex = xSemaphoreCreateMutex();
  return (*mutex == NULL) ? ERR_MEM : ERR_OK;
}

void sys_mutex_free(sys_mutex_t *mutex)
{
  if ((mutex != NULL) && (*mutex != SYS_MBOX_NULL)) {
    vSemaphoreDelete(*mutex);
    *mutex = NULL;
  }
}

void sys_mutex_lock(sys_mutex_t *mutex)
{
  if ((mutex != NULL) && (*mutex != NULL)) {
    (void)xSemaphoreTake(*mutex, portMAX_DELAY);
  }
}

void sys_mutex_unlock(sys_mutex_t *mutex)
{
  if ((mutex != NULL) && (*mutex != NULL)) {
    (void)xSemaphoreGive(*mutex);
  }
}

/* ------------------------------ 邮箱 ------------------------------ */
err_t sys_mbox_new(sys_mbox_t *mbox, int size)
{
  if (mbox == NULL) {
    return ERR_ARG;
  }
  *mbox = xQueueCreate((UBaseType_t)size, sizeof(void *));
  return (*mbox == NULL) ? ERR_MEM : ERR_OK;
}

void sys_mbox_free(sys_mbox_t *mbox)
{
  if ((mbox != NULL) && (*mbox != SYS_MBOX_NULL)) {
    (void)uxQueueMessagesWaiting(*mbox);
    vQueueDelete(*mbox);
    *mbox = SYS_MBOX_NULL;
  }
}

void sys_mbox_post(sys_mbox_t *mbox, void *msg)
{
  void *m = msg;
  if ((mbox != NULL) && (*mbox != SYS_MBOX_NULL)) {
    while (xQueueSend(*mbox, &m, portMAX_DELAY) != pdTRUE) {
    }
  }
}

err_t sys_mbox_trypost(sys_mbox_t *mbox, void *msg)
{
  void *m = msg;
  if ((mbox == NULL) || (*mbox == SYS_MBOX_NULL)) {
    return ERR_ARG;
  }
  return (xQueueSend(*mbox, &m, 0) == pdTRUE) ? ERR_OK : ERR_MEM;
}

err_t sys_mbox_trypost_fromisr(sys_mbox_t *mbox, void *msg)
{
  BaseType_t hpw = pdFALSE;
  void *m = msg;
  if ((mbox == NULL) || (*mbox == SYS_MBOX_NULL)) {
    return ERR_ARG;
  }
  return (xQueueSendFromISR(*mbox, &m, &hpw) == pdTRUE) ? ERR_OK : ERR_MEM;
}

u32_t sys_arch_mbox_fetch(sys_mbox_t *mbox, void **msg, u32_t timeout)
{
  void *m = NULL;
  TickType_t ticks;
  TimeOut_t to;

  if ((mbox == NULL) || (*mbox == SYS_MBOX_NULL)) {
    return SYS_ARCH_TIMEOUT;
  }
  if (timeout == 0U) {
    if (xQueueReceive(*mbox, &m, portMAX_DELAY) != pdTRUE) {
      return SYS_ARCH_TIMEOUT;
    }
    if (msg != NULL) {
      *msg = m;
    }
    return 0U;
  }
  ticks = pdMS_TO_TICKS(timeout);
  vTaskSetTimeOutState(&to);
  if (xQueueReceive(*mbox, &m, ticks) != pdTRUE) {
    return SYS_ARCH_TIMEOUT;
  }
  if (msg != NULL) {
    *msg = m;
  }
  return (u32_t)((xTaskGetTickCount() - to.xTimeOnEntering) * portTICK_PERIOD_MS);
}

u32_t sys_arch_mbox_tryfetch(sys_mbox_t *mbox, void **msg)
{
  void *m = NULL;
  if ((mbox == NULL) || (*mbox == SYS_MBOX_NULL)) {
    return SYS_MBOX_EMPTY;
  }
  if (xQueueReceive(*mbox, &m, 0) != pdTRUE) {
    return SYS_MBOX_EMPTY;
  }
  if (msg != NULL) {
    *msg = m;
  }
  return 0U;
}

/* ------------------------------ 线程 ------------------------------ */
sys_thread_t sys_thread_new(const char *name, lwip_thread_fn thread, void *arg,
                            int stacksize, int prio)
{
  TaskHandle_t task = NULL;
  (void)xTaskCreate((TaskFunction_t)thread, name,
                    (configSTACK_DEPTH_TYPE)stacksize, arg, (UBaseType_t)prio,
                    &task);
  return task;
}

#endif /* !NO_SYS */
