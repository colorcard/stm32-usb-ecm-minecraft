#ifndef _CONFIG__ADV_H
#define _CONFIG__ADV_H

/*
 * UCraft 高级配置（STM32 移植版）。
 * 关闭压缩与在线模式，以省去 zlib / mbedTLS 依赖。
 */

#define ENDIAN 1 /* 1: little, 0: big */

#define TICK_TIME_MS 10
#define MEM_CHUNK_SIZE 256    /* Allocation granularity */
#define MEM_CHUNK_THRESHOLD 2 /* Number of chunks to allocate before reallocating */

#define MAX_SEND_FRAGMENT_SIZE 4096 /* 每轮最多向 lwIP 推入的字节数 */
#define MAX_STRING_SIZE 512
#define LOG_BUFFER_SIZE 512
#define READBUFSIZE 2048

#define AUTH_HOST "sessionserver.mojang.com"
#define AUTH_HOST_PORT "443"
#define AUTH_TIMEOUT 800

#define PLAYER_BASE 0x4

#endif /* _CONFIG__ADV_H */
