/**
 * @file sys_arch.h
 * @brief lwIP 在 FreeRTOS 上的 sys_arch 类型（NO_SYS=0）。
 */
#ifndef LWIP_ARCH_SYS_ARCH_H
#define LWIP_ARCH_SYS_ARCH_H

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"

typedef SemaphoreHandle_t sys_sem_t;
typedef SemaphoreHandle_t sys_mutex_t;
typedef QueueHandle_t sys_mbox_t;
typedef TaskHandle_t sys_thread_t;

#define SYS_MBOX_NULL NULL
#define SYS_SEM_NULL  NULL

#define sys_sem_valid(sem) (((sem) != NULL) && (*(sem) != SYS_SEM_NULL))
#define sys_sem_set_invalid(sem) \
  do {                            \
    if ((sem) != NULL) {          \
      *(sem) = SYS_SEM_NULL;      \
    }                             \
  } while (0)
#define sys_mbox_valid(mbox) (((mbox) != NULL) && (*(mbox) != SYS_MBOX_NULL))
#define sys_mbox_set_invalid(mbox) \
  do {                             \
    if ((mbox) != NULL) {          \
      *(mbox) = SYS_MBOX_NULL;     \
    }                              \
  } while (0)

void sys_arch_msleep(uint32_t ms);

#endif /* LWIP_ARCH_SYS_ARCH_H */
