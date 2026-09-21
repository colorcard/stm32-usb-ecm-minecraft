/**
 * @file cc.h
 * @brief lwIP 平台抽象层（ARM GCC / newlib，小端）。
 */
#ifndef LWIP_ARCH_CC_H
#define LWIP_ARCH_CC_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

void rp_lwip_diag_printf(const char *fmt, ...);
void rp_lwip_assert_failed(const char *msg, const char *file, int line);

#ifdef __cplusplus
}
#endif

#define LWIP_PLATFORM_DIAG(x) do { rp_lwip_diag_printf x; } while (0)
#define LWIP_PLATFORM_ASSERT(x) \
  do { rp_lwip_assert_failed((x), __FILE__, __LINE__); } while (0)

#define LWIP_RAND() ((u32_t)rand())

/** 目标为小端 Cortex-M4。 */
#define BYTE_ORDER LITTLE_ENDIAN

#endif /* LWIP_ARCH_CC_H */
