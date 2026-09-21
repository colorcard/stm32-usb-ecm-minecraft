#ifndef _mc_server_h_
#define _mc_server_h_

#include "rp_common_headfile.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 监听端口（Minecraft 默认）。 */
#define MC_SERVER_PORT   25565

/**
 * @brief 启动 Minecraft Java 版协议服务器（状态 Ping + 离线登录）。
 * @return RP_OK 表示监听成功。
 * @note 需先完成 lwIP 初始化。
 */
rp_status_t mc_server_init(void);

/**
 * @brief 周期服务：刷新连接状态（当前主要用于超时回收）。
 * @return 无。
 */
void mc_server_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* _mc_server_h_ */
