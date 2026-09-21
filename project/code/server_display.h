#ifndef _server_display_h_
#define _server_display_h_

#include "rp_common_headfile.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 LCD 并绘制静态框架（标题/端口标签）。
 * @return 无。
 */
void server_display_init(void);

/**
 * @brief 周期刷新动态信息（约每秒一次，仅重绘变化行）。
 * @return 无。
 */
void server_display_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* _server_display_h_ */
