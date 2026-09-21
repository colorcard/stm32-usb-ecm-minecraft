#include "log.h"
#include "config.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void printl(const char *type, const char *fmt, ...)
{
    /* 目标端无可靠 stdio（printf 会触发 newlib 堆/栈冲突而硬故障），
     * 默认丢弃日志；需要时可用 UART 安全重定向。 */
    (void)type;
    (void)fmt;
}
