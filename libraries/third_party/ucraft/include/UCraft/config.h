#ifndef _CONFIG_H
#define _CONFIG_H
#include "config_adv.h"

#define PORT 25565

#define MAX_PLAYERS 1

/* 关闭压缩与在线模式：省去 zlib / mbedTLS 依赖。 */
/* #define COMPRESSION */
#define COMPRESSION_THRESHOLD 2048
/* #define ONLINE_MODE */
/* #define ONLINE_MODE_AUTH */

#define SPAWN_X 8.00
#define SPAWN_Y 27.00
#define SPAWN_Z 8.00

#define GAMEMODE 0    /* 0: Survival, 1: Creative, 2: Adventure, 3: Spectator. */
#define DIFFICULTY 2  /* 0: peaceful, 1: easy, 2: normal, 3: hard */
#define VIEWDISTANCE 1
#define WORLD_SEED 67
#define RESPAWNSCREEN 1

/* 与客户端 26.3 对齐（26.2+ 协议 776）。已知包版本与客户端一致，客户端会
 * 确认自带核心数据包，服务器可跳过下发注册表、直接 Finish Configuration。 */
#define CLIENT_VERSION "26.3"
#define PROTOCOL_VERSION 776
#define LONG_PROTOCOL_VERSION "\\u00A7c STM32 USB-ECM: 26.3"
#define MOTD "\\u00A7a\\u00A7lSTM32 USB-ECM\\u00A7r \\u00A77Minecraft server in C"

#endif /* _CONFIG_H */
