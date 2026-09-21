/**
 * @file server_display.c
 * @brief 在 ST7789 LCD 上显示服务器基本信息。
 *
 * 使用内置 5x7 点阵字体，按“标签 + 值”两色呈现；每秒刷新一次，仅重绘
 * 内容发生变化的行，减少 SPI 占用。
 */
#include "server_display.h"

#include <stdio.h>
#include <string.h>

#include "lcd_font5x7.h"
#include "lwip/ip_addr.h"
#include "mc_server.h"
#include "rp_device_lcd_hw.h"
#include "rp_device_usb_ecm.h"
#include "rp_dhcpd.h"
#include "rp_lwip.h"

/** @brief 放大倍数与版面参数。 */
#define DISP_SCALE   2U
#define DISP_GLYPH_W (LCD_FONT_WIDTH * DISP_SCALE)   /* 10 */
#define DISP_GLYPH_H (LCD_FONT_HEIGHT * DISP_SCALE)  /* 14 */
#define DISP_CELL_W  (DISP_GLYPH_W + DISP_SCALE)     /* 12 */
#define DISP_LINE_H  (DISP_GLYPH_H + 2U)             /* 16 */
#define DISP_LINES   8U
#define DISP_Y0      6U
#define DISP_STEP    18U
#define DISP_X0      4U
#define DISP_VALUE_X (DISP_X0 + 7U * DISP_CELL_W)    /* 88 */
#define DISP_GLYPH_Y ((DISP_LINE_H - DISP_GLYPH_H) / 2U)

/** @brief RGB565 颜色。 */
#define RGB565(r, g, b) \
  ((uint16_t)((((r) & 0xF8U) << 8) | (((g) & 0xFCU) << 3) | (((b) & 0xF8U) >> 3)))
#define COL_BG    RGB565(0U, 0U, 0U)
#define COL_TITLE RGB565(255U, 196U, 0U)
#define COL_LABEL RGB565(0U, 180U, 255U)
#define COL_VALUE RGB565(255U, 255U, 255U)
#define COL_OK    RGB565(0U, 220U, 0U)
#define COL_ERR   RGB565(255U, 60U, 60U)

/** @brief 行缓冲（整行像素）。 */
static uint16_t s_line[LCD_HW_WIDTH * DISP_LINE_H];
/** @brief 每行动态值缓存，用于判断是否需要重绘。 */
static char s_cache_value[DISP_LINES][40];
static uint16_t s_cache_color[DISP_LINES];
static uint8_t s_cache_valid[DISP_LINES];
/** @brief 上次刷新时刻。 */
static uint32_t s_last_ms;
/** @brief 端口字符串（静态行）。 */
static const char s_port_label[] = "PORT";
static const char s_port_value[] = "25565";

/**
 * @brief 字符到字库索引（小写映射为大写）。
 * @return 索引；不可显示字符返回 -1。
 */
static int font_index(char ch)
{
  unsigned c = (unsigned char)ch;

  if ((c >= (unsigned)'a') && (c <= (unsigned)'z')) {
    c -= 32U;
  }
  if ((c < LCD_FONT_FIRST) || (c > LCD_FONT_LAST)) {
    return -1;
  }
  return (int)(c - LCD_FONT_FIRST);
}

static void draw_glyph(uint16_t x, uint16_t y, char ch, uint16_t color)
{
  int idx = font_index(ch);

  if (idx < 0) {
    return;
  }
  for (uint32_t row = 0U; row < LCD_FONT_HEIGHT; ++row) {
    uint8_t bits = s_lcd_font[idx][row];
    for (uint32_t col = 0U; col < LCD_FONT_WIDTH; ++col) {
      if ((bits & (1U << (LCD_FONT_WIDTH - 1U - col))) != 0U) {
        for (uint32_t dy = 0U; dy < DISP_SCALE; ++dy) {
          for (uint32_t dx = 0U; dx < DISP_SCALE; ++dx) {
            uint32_t px = (uint32_t)x + col * DISP_SCALE + dx;
            uint32_t py = (uint32_t)y + row * DISP_SCALE + dy;
            if ((px < LCD_HW_WIDTH) && (py < DISP_LINE_H)) {
              s_line[py * LCD_HW_WIDTH + px] = color;
            }
          }
        }
      }
    }
  }
}

static void draw_text(uint16_t x, const char *s, uint16_t color)
{
  while (*s != '\0') {
    if ((uint32_t)x + DISP_GLYPH_W > LCD_HW_WIDTH) {
      break;
    }
    draw_glyph(x, DISP_GLYPH_Y, *s, color);
    x = (uint16_t)(x + DISP_CELL_W);
    ++s;
  }
}

static void line_fill_bg(void)
{
  for (uint32_t i = 0U; i < (uint32_t)LCD_HW_WIDTH * DISP_LINE_H; ++i) {
    s_line[i] = COL_BG;
  }
}

static void flush_line(uint16_t y)
{
  (void)lcd_hw_write_area_rgb565(0U, y, LCD_HW_WIDTH, DISP_LINE_H, s_line);
}

/** @brief 整屏填背景色。 */
static void clear_screen(void)
{
  line_fill_bg();
  for (uint16_t y = 0U; y < LCD_HW_HEIGHT; y = (uint16_t)(y + DISP_LINE_H)) {
    (void)lcd_hw_write_area_rgb565(0U, y, LCD_HW_WIDTH, DISP_LINE_H, s_line);
  }
}

static uint16_t line_y(uint32_t index)
{
  return (uint16_t)(DISP_Y0 + index * DISP_STEP);
}

static void draw_title(const char *text)
{
  line_fill_bg();
  draw_text(DISP_X0, text, COL_TITLE);
  flush_line(line_y(0U));
}

static void draw_kv(uint32_t index, const char *label, const char *value,
                    uint16_t color)
{
  line_fill_bg();
  draw_text(DISP_X0, label, COL_LABEL);
  draw_text(DISP_VALUE_X, value, color);
  flush_line(line_y(index));
}

/**
 * @brief 仅在值或颜色变化时重绘该行。
 */
static void draw_kv_cached(uint32_t index, const char *label, const char *value,
                           uint16_t color)
{
  if ((s_cache_valid[index] != 0U) &&
      (s_cache_color[index] == color) &&
      (strcmp(s_cache_value[index], value) == 0)) {
    return;
  }
  draw_kv(index, label, value, color);
  (void)snprintf(s_cache_value[index], sizeof(s_cache_value[index]), "%s",
                 value);
  s_cache_color[index] = color;
  s_cache_valid[index] = 1U;
}

void server_display_init(void)
{
  struct netif *n = rp_lwip_netif();
  const ip4_addr_t *ip;
  char ipbuf[20];
  char macbuf[24];

  (void)memset(s_cache_valid, 0, sizeof(s_cache_valid));

  clear_screen();
  draw_title("STM32 USB-ECM SERVER");
  draw_kv(5U, s_port_label, s_port_value, COL_VALUE);

  if (n != NULL) {
    ip = netif_ip4_addr(n);
    (void)snprintf(ipbuf, sizeof(ipbuf), "%u.%u.%u.%u", ip4_addr1_16(ip),
                   ip4_addr2_16(ip), ip4_addr3_16(ip), ip4_addr4_16(ip));
    (void)snprintf(macbuf, sizeof(macbuf), "%02X:%02X:%02X:%02X:%02X:%02X",
                   n->hwaddr[0], n->hwaddr[1], n->hwaddr[2], n->hwaddr[3],
                   n->hwaddr[4], n->hwaddr[5]);
    draw_kv_cached(1U, "IP", ipbuf, COL_VALUE);
    draw_kv_cached(2U, "MAC", macbuf, COL_VALUE);
  }
}

void server_display_poll(void)
{
  struct netif *n = rp_lwip_netif();
  const ip4_addr_t *ip;
  char ipbuf[20];
  char macbuf[24];
  char connbuf[40];
  uint32_t now = HAL_GetTick();

  if (n == NULL) {
    return;
  }
  if ((now - s_last_ms) < 1000U) {
    return;
  }
  s_last_ms = now;

  ip = netif_ip4_addr(n);
  (void)snprintf(ipbuf, sizeof(ipbuf), "%u.%u.%u.%u", ip4_addr1_16(ip),
                 ip4_addr2_16(ip), ip4_addr3_16(ip), ip4_addr4_16(ip));
  (void)snprintf(macbuf, sizeof(macbuf), "%02X:%02X:%02X:%02X:%02X:%02X",
                 n->hwaddr[0], n->hwaddr[1], n->hwaddr[2], n->hwaddr[3],
                 n->hwaddr[4], n->hwaddr[5]);

  draw_kv_cached(1U, "IP", ipbuf, COL_VALUE);
  draw_kv_cached(2U, "MAC", macbuf, COL_VALUE);

  if (rp_dhcpd_lease_active() != 0) {
    draw_kv_cached(3U, "HOST", "192.168.7.2", COL_VALUE);
  } else {
    draw_kv_cached(3U, "HOST", "WAITING...", COL_VALUE);
  }

  if (usb_ecm_is_configured() != 0) {
    draw_kv_cached(4U, "USB", "CONFIGURED", COL_OK);
  } else {
    draw_kv_cached(4U, "USB", "NO LINK", COL_ERR);
  }

  (void)snprintf(connbuf, sizeof(connbuf), "%lu/%u TOTAL %lu",
                 (unsigned long)mc_server_active_conns(), 8U,
                 (unsigned long)mc_server_total_conns());
  draw_kv_cached(6U, "CONN", connbuf, COL_VALUE);
  draw_kv_cached(7U, "LAST", mc_server_last_player(), COL_VALUE);
}
