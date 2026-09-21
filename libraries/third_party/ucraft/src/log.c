#include "log.h"
#include "config.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void printl(const char *type, const char *fmt, ...)
{
    char buffer[LOG_BUFFER_SIZE];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    printf("[%s]: %s", type, buffer);
}