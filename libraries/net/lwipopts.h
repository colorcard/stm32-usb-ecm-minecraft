/**
 * @file lwipopts.h
 * @brief STM32 USB-ECM 工程的 lwIP 配置（裸机 NO_SYS，IPv4，TCP/UDP）。
 */
#ifndef LWIP_LWIPOPTS_H
#define LWIP_LWIPOPTS_H

/* ------------------------------------------------------------------ */
/* 运行模式：裸机主循环轮询，无 RTOS。 */
/* ------------------------------------------------------------------ */
#define NO_SYS                      1
#define SYS_LIGHTWEIGHT_PROT        0
#define LWIP_NETCONN                0
#define LWIP_SOCKET                 0
#define LWIP_TCPIP_CORE_LOCKING     0
#define LWIP_TIMERS                 1
#define LWIP_TIMERS_CUSTOM          0

/* ------------------------------------------------------------------ */
/* 协议族：只启 IPv4。 */
/* ------------------------------------------------------------------ */
#define LWIP_IPV4                   1
#define LWIP_IPV6                   0
#define LWIP_ARP                    1
#define LWIP_ETHERNET               1
#define LWIP_ICMP                   1
#define LWIP_IGMP                   0
#define LWIP_DNS                    0
/* 仅为启用 IP_ACCEPT_LINK_LAYER_ADDRESSING（接受源地址 0.0.0.0 的 DHCP 请求），
 * 不调用 dhcp_start，故不产生 DHCP 客户端流量。 */
#define LWIP_DHCP                   1
#define LWIP_AUTOIP                 0
#define LWIP_UDP                    1
#define LWIP_TCP                    1
#define LWIP_RAW                    0
#define LWIP_IP_FRAG                0
#define LWIP_IP_REASSEMBLY          0

/* ------------------------------------------------------------------ */
/* 内存：128KB RAM，尽量用静态池，缩小堆。 */
/* ------------------------------------------------------------------ */
#define MEM_LIBC_MALLOC             0
#define MEMP_MEM_MALLOC             0
#define MEM_ALIGNMENT               4
#define MEM_SIZE                    (8 * 1024)
#define MEMP_OVERFLOW_CHECK         0
#define MEMP_SANITY_CHECK           0

#define MEMP_NUM_PBUF               16
#define MEMP_NUM_RAW_PCB            4
#define MEMP_NUM_UDP_PCB            4
#define MEMP_NUM_TCP_PCB            4
#define MEMP_NUM_TCP_PCB_LISTEN     2
#define MEMP_NUM_TCP_SEG            16
#define MEMP_NUM_REASSDATA          0
#define MEMP_NUM_FRAG_PBUF          0
#define MEMP_NUM_ARP_QUEUE          8
#define MEMP_NUM_SYS_TIMEOUT        8

#define PBUF_POOL_SIZE              8
#define PBUF_POOL_BUFSIZE           1536

/* ------------------------------------------------------------------ */
/* TCP：单客户端场景，MSS 按以太网 MTU。 */
/* ------------------------------------------------------------------ */
#define LWIP_TCP_SACK_OUT           0
#define TCP_MSS                     1460
#define TCP_WND                     (2 * TCP_MSS)
#define TCP_SND_BUF                 (2 * TCP_MSS)
#define TCP_LISTEN_BACKLOG          1
#define LWIP_TCP_KEEPALIVE          0
#define LWIP_WND_SCALE              0

/* ------------------------------------------------------------------ */
/* 网络接口。 */
/* ------------------------------------------------------------------ */
#define LWIP_NETIF_HOSTNAME         0
#define LWIP_NETIF_STATUS_CALLBACK  1
#define LWIP_NETIF_LINK_CALLBACK    1
#define LWIP_NETIF_TX_SINGLE_PBUF   1
#define LWIP_CHECKSUM_CTRL_PER_NETIF 0
#define LWIP_NETIF_HWADDRHINT       0

/* ------------------------------------------------------------------ */
/* 关闭调试与统计，省 Flash/RAM。 */
/* ------------------------------------------------------------------ */
#define LWIP_DEBUG                  0
#define LWIP_STATS                  0
#define LWIP_STATS_DISPLAY          0
#define LWIP_DBG_MIN_LEVEL          LWIP_DBG_LEVEL_OFF
#define LWIP_NOASSERT               0

/* USB 点对点链路可靠，省去入站校验和计算。 */
#define CHECKSUM_CHECK_IP           0
#define CHECKSUM_CHECK_UDP          0

/* ------------------------------------------------------------------ */
/* 其他。 */
/* ------------------------------------------------------------------ */
#define LWIP_HAVE_LOOPIF            0
#define LWIP_SINGLE_NETIF           1
#define LWIP_SO_RCVBUF              0
#define LWIP_NETIF_LOOPBACK         0
#define LWIP_LOOPIF_MULTICAST       0
#define LWIP_TCPIP_TIMEOUT          0
#define LWIP_MULTICAST_TX_OPTIONS   0
#define LWIP_BROADCAST_PING         1
#define LWIP_RANDOMIZE_INITIAL_LOCAL_PORTS 0

#endif /* LWIP_LWIPOPTS_H */
