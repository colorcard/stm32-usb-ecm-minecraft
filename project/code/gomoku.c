#include "gomoku.h"

#include <stdio.h>
#include <string.h>

#include "stm32g4xx_hal.h"

#include "rapfi_pattern_tables.h"

#define CCMRAM __attribute__((section(".ccmram")))

/** @brief 每格最多所属的 5 连窗口数（4 方向 × 5 个偏移）。 */
#define GWIN_PER_CELL   20
/** @brief 5 连窗口总数上限（横 165 + 竖 165 + 斜 121×2）。 */
#define GWIN_MAX        600
/** @brief 搜索最大层数。 */
#define MAX_PLY         16
/** @brief 候选点数量上限。 */
#define MAX_CAND        128
/** @brief 根节点保留的候选数。 */
#define ROOT_CAND       16
/** @brief 内部节点保留的候选数。 */
#define NODE_CAND       16
/** @brief 胜负分（缩放到 int16，便于置换表存储）。 */
#define SCORE_WIN       30000
/** @brief 搜索无穷大。 */
#define SCORE_INF       (SCORE_WIN + 1000)
/** @brief 视为“必杀分”的阈值。 */
#define SCORE_MATE      (SCORE_WIN - MAX_PLY)
/** @brief 静态评估上限：必须远小于胜负分，否则引擎会把好局面误判成必胜。 */
#define SCORE_EVAL_MAX  6000

/** @brief 置换表：2^13 项 × 8 字节 = 64KB。 */
#define TT_BITS         11
#define TT_SIZE         (1U << TT_BITS)
#define TT_MASK         (TT_SIZE - 1U)

/** @brief VCF（连四威胁）搜索上限。 */
#define VCF_MAX_PLY     12
#define VCF_MAX_CAND    48

/** @brief 窗口内本方子数对应的分值（下标 0..5）。 */
static const int32_t s_win_score[6] = {0, 1, 6, 30, 200, SCORE_WIN};

/* ------------------------------ 棋盘状态 ------------------------------ */

CCMRAM static uint8_t s_cell[GOMOKU_CELLS];
CCMRAM static uint8_t s_near[GOMOKU_CELLS]; /* 附近 2 格内的邻子计数，增量维护 */
static uint8_t s_hist_cell[GOMOKU_CELLS];
static uint8_t s_hist_side[GOMOKU_CELLS];
static int s_hist_n;
static uint8_t s_winner;
/** @brief Zobrist 键与当前局面哈希。 */
static uint32_t s_zob[2][GOMOKU_CELLS];
static uint32_t s_hash;

/* SWAR 位棋盘：每方 4 个视图（行/列/↘/↙），每条线一个 lane（bit=x）。
   连五判定用 m&(m>>1)&(m>>2)&(m>>3)&(m>>4)，寄存器内并行。 */
CCMRAM static uint32_t s_bb[2][4][29];
/** @brief 中心权重表（越靠中心越高），用于开局引导。 */
static int16_t s_ctr_tab[GOMOKU_CELLS];
/** @brief 每方中心权重和（增量维护）。 */
static int32_t s_ctr[3];

/* ------------------------------ 窗口索引 ------------------------------ */

CCMRAM static uint16_t s_win[GWIN_MAX][5];
static uint16_t s_nwin;
CCMRAM static uint8_t s_cell_win[GOMOKU_CELLS][GWIN_PER_CELL];
CCMRAM static uint8_t s_cell_win_n[GOMOKU_CELLS];
/** @brief 每个 5 连窗口内各方子数（增量维护，下标 1/2）。 */
CCMRAM static uint8_t s_win_cnt[3][GWIN_MAX];
/** @brief 每方“含 4 子”的窗口数（增量维护，>0 才可能有成五点）。 */
CCMRAM static uint16_t s_four_win[3];

/** @brief 棋型评估用的“线”（行/列/两向斜线）。 */
#define GLINE_MAX 96
CCMRAM static uint8_t s_line_cells[GLINE_MAX][GOMOKU_N];
CCMRAM static uint8_t s_line_len[GLINE_MAX];
static uint8_t s_line_dir[GLINE_MAX];
static int s_nline;
/** @brief 增量棋型分：每条线每方分值，以及双方总分。 */
CCMRAM static int32_t s_line_pat[GLINE_MAX][3];
static int32_t s_pat[3];
/** @brief 每格在 4 个方向上所属的线（0xFF 表示不在任何 >=5 的线上）。 */
static uint8_t s_cell_line[GOMOKU_CELLS][4];

/* ------------------------------ 置换表 ------------------------------ */

typedef struct {
  uint32_t key;
  int16_t score;
  uint8_t move; /* 最佳着法格号，255 表示无 */
  uint8_t meta; /* depth<<2 | flag(0=exact,1=lower,2=upper) */
} tt_entry_t;

static tt_entry_t s_tt[TT_SIZE];

/* ------------------------------ 搜索状态 ------------------------------ */

static uint16_t s_cand[MAX_PLY][MAX_CAND];
static int32_t s_cand_score[MAX_PLY][MAX_CAND];
static int s_ncand[MAX_PLY];
static uint16_t s_killer[MAX_PLY][2];
static uint16_t s_vcf_cand[VCF_MAX_PLY][VCF_MAX_CAND];
static int s_vcf_n[VCF_MAX_PLY];
static uint32_t s_nodes;
static uint32_t s_deadline;
static int s_time_limited;
static int s_abort;
static gomoku_stop_fn s_stop_hook;
/** @brief VCF 节点预算，避免不限时时指数爆炸。 */
static uint32_t s_vcf_nodes;
#define VCF_NODE_LIMIT 400000U
/** @brief VCT 节点预算。 */
static uint32_t s_vct_nodes;
#define VCT_NODE_LIMIT 300000U
/* 三角 PV 表（每层的最佳线路），以及根 PV 供协议回传 */
static uint8_t s_pv[MAX_PLY][MAX_PLY];
static uint8_t s_pv_len[MAX_PLY];
static uint8_t s_root_pv[MAX_PLY];
static uint8_t s_root_pv_len;

static int32_t pattern_value(int len, int open_l, int open_r);
static void pat_refresh_all(void);
static void bb_set(int idx, int side);
static void bb_clear(int idx, int side);

/** @brief 简易 xorshift。 */
static uint32_t xorshift(uint32_t *state)
{
  uint32_t x = *state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  *state = x;
  return x;
}

void gomoku_init(void)
{
  static const int dx[4] = {1, 0, 1, 1};
  static const int dy[4] = {0, 1, 1, -1};
  uint32_t seed = 0x9E3779B9U;
  int dir;
  int x;
  int y;
  int s;
  int i;

  s_nwin = 0U;
  memset(s_cell_win_n, 0, sizeof(s_cell_win_n));

  for (dir = 0; dir < 4; ++dir) {
    for (y = 0; y < GOMOKU_N; ++y) {
      for (x = 0; x < GOMOKU_N; ++x) {
        int k;
        int ok = 1;
        int ex = x + dx[dir] * 4;
        int ey = y + dy[dir] * 4;
        if ((ex < 0) || (ex >= GOMOKU_N) || (ey < 0) || (ey >= GOMOKU_N)) {
          ok = 0;
        }
        if ((s_nwin >= GWIN_MAX) || (ok == 0)) {
          continue;
        }
        for (k = 0; k < 5; ++k) {
          int cx = x + dx[dir] * k;
          int cy = y + dy[dir] * k;
          int idx = cy * GOMOKU_N + cx;
          uint8_t n = s_cell_win_n[idx];
          s_win[s_nwin][k] = (uint16_t)idx;
          if (n < GWIN_PER_CELL) {
            s_cell_win[idx][n] = (uint8_t)s_nwin;
            s_cell_win_n[idx] = (uint8_t)(n + 1U);
          }
        }
        ++s_nwin;
      }
    }
  }

  for (s = 0; s < 2; ++s) {
    for (i = 0; i < GOMOKU_CELLS; ++i) {
      s_zob[s][i] = xorshift(&seed) | 1U;
    }
  }
  memset(s_tt, 0, sizeof(s_tt));

  for (i = 0; i < GOMOKU_CELLS; ++i) {
    int ct_x = i % GOMOKU_N;
    int ct_y = i / GOMOKU_N;
    int dist = ((ct_x > 7) ? (ct_x - 7) : (7 - ct_x)) +
               ((ct_y > 7) ? (ct_y - 7) : (7 - ct_y));
    s_ctr_tab[i] = (int16_t)((7 - dist) * 4);
  }

  /* 启用 DWT 周期计数器（搜索时限用）。 */
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0U;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

  /* 生成行/列/斜线（长度 >= 5）。 */
  {
    int d;
    static const int ddx[4] = {1, 0, 1, 1};
    static const int ddy[4] = {0, 1, 1, -1};
    s_nline = 0;
    for (d = 0; d < 4; ++d) {
      int sx;
      int sy;
      for (sy = 0; sy < GOMOKU_N; ++sy) {
        for (sx = 0; sx < GOMOKU_N; ++sx) {
          int k = 0;
          int cx = sx;
          int cy = sy;
          /* 只从线的起点开始枚举：前一个格子必须在盘外或方向不同。 */
          int px = sx - ddx[d];
          int py = sy - ddy[d];
          if ((px >= 0) && (px < GOMOKU_N) && (py >= 0) && (py < GOMOKU_N)) {
            continue;
          }
          while ((cx >= 0) && (cx < GOMOKU_N) && (cy >= 0) &&
                 (cy < GOMOKU_N) && (k < GOMOKU_N) && (s_nline < GLINE_MAX)) {
            s_line_cells[s_nline][k] = (uint8_t)(cy * GOMOKU_N + cx);
            ++k;
            cx += ddx[d];
            cy += ddy[d];
          }
          if ((k >= 5) && (s_nline < GLINE_MAX)) {
            s_line_len[s_nline] = (uint8_t)k;
            s_line_dir[s_nline] = (uint8_t)d;
            ++s_nline;
          }
        }
      }
    }
    /* 建立格 -> 线 索引，并清零增量棋型分。 */
    memset(s_cell_line, 0xFF, sizeof(s_cell_line));
    memset(s_line_pat, 0, sizeof(s_line_pat));
    s_pat[0] = 0;
    s_pat[1] = 0;
    s_pat[2] = 0;
    for (i = 0; i < s_nline; ++i) {
      int dd = (int)s_line_dir[i];
      int kk;
      for (kk = 0; kk < (int)s_line_len[i]; ++kk) {
        s_cell_line[s_line_cells[i][kk]][dd] = (uint8_t)i;
      }
    }
  }
}

void gomoku_new(void)
{
  memset(s_cell, GOMOKU_EMPTY, sizeof(s_cell));
  memset(s_near, 0, sizeof(s_near));
  memset(s_win_cnt, 0, sizeof(s_win_cnt));
  memset(s_bb, 0, sizeof(s_bb));
  s_four_win[0] = 0;
  s_four_win[1] = 0;
  s_four_win[2] = 0;
  memset(s_tt, 0, sizeof(s_tt));
  memset(s_line_pat, 0, sizeof(s_line_pat));
  s_ctr[0] = 0;
  s_ctr[1] = 0;
  s_ctr[2] = 0;
  s_pat[0] = 0;
  s_pat[1] = 0;
  s_pat[2] = 0;
  s_hash = 0U;
  s_hist_n = 0;
  s_winner = GOMOKU_EMPTY;
  pat_refresh_all();
}

/**
 * @brief 落子/撤销时增量维护“附近有子”计数。
 * @param idx 格子索引。
 * @param delta +1 落子，-1 撤销。
 * @return 无。
 */
static void near_update(int idx, int delta)
{
  int x = idx % GOMOKU_N;
  int y = idx / GOMOKU_N;
  int dy;

  for (dy = -2; dy <= 2; ++dy) {
    int ny = y + dy;
    int dx;
    if ((ny < 0) || (ny >= GOMOKU_N)) {
      continue;
    }
    for (dx = -2; dx <= 2; ++dx) {
      int nx = x + dx;
      int j;
      if ((nx < 0) || (nx >= GOMOKU_N) || ((dx == 0) && (dy == 0))) {
        continue;
      }
      j = ny * GOMOKU_N + nx;
      if (delta > 0) {
        if (s_near[j] < 255U) {
          ++s_near[j];
        }
      } else if (s_near[j] > 0U) {
        --s_near[j];
      }
    }
  }
}

/**
 * @brief 重算某条线的双方棋型分并增量更新总分。
 * @param line 线编号。
 * @return 无。
 */
static void line_recompute(int line)
{
  const uint8_t *cells = s_line_cells[line];
  int len = (int)s_line_len[line];
  int32_t p1 = 0;
  int32_t p2 = 0;
  int i = 0;

  while (i < len) {
    uint8_t v = s_cell[cells[i]];
    int j;
    int run;
    int open_l;
    int open_r;
    if (v == GOMOKU_EMPTY) {
      ++i;
      continue;
    }
    j = i;
    while ((j < len) && (s_cell[cells[j]] == v)) {
      ++j;
    }
    run = j - i;
    open_l = (i > 0) && (s_cell[cells[i - 1]] == GOMOKU_EMPTY);
    open_r = (j < len) && (s_cell[cells[j]] == GOMOKU_EMPTY);
    if (v == GOMOKU_BLACK) {
      p1 += pattern_value(run, open_l, open_r);
    } else {
      p2 += pattern_value(run, open_l, open_r);
    }
    i = j;
  }
  s_pat[1] += p1 - s_line_pat[line][1];
  s_pat[2] += p2 - s_line_pat[line][2];
  s_line_pat[line][1] = p1;
  s_line_pat[line][2] = p2;
}

/** @brief 更新经过 idx 的四条线的棋型分。 */
static void pat_update(int idx)
{
  int d;
  for (d = 0; d < 4; ++d) {
    uint8_t line = s_cell_line[idx][d];
    if (line != 0xFFU) {
      line_recompute((int)line);
    }
  }
}

/* --------------------- Rapfi 逐格棋型码（查表，增量维护） --------------------- */

/* Pattern 枚举（与 refer/rapfi 一致） */
enum {
  PAT_DEAD = 0, PAT_OL, PAT_B1, PAT_F1, PAT_B2, PAT_F2, PAT_F2A, PAT_F2B,
  PAT_B3, PAT_B3S, PAT_F3, PAT_F3S, PAT_B4, PAT_B4S, PAT_F4, PAT_F5,
  PATTERN_NB
};

/* Pattern4：4 方向组合威胁强度（freestyle，无禁手） */
enum {
  P4_NONE = 0, P4_FORBID, P4_L_FLEX2, P4_K_BLOCK3, P4_J_FLEX2_2X,
  P4_I_BLOCK3_PLUS, P4_H_FLEX3, P4_G_FLEX3_PLUS, P4_F_FLEX3_2X,
  P4_E_BLOCK4, P4_D_BLOCK4_PLUS, P4_C_BLOCK4_FLEX3, P4_B_FLEX4,
  P4_A_FIVE, PATTERN4_NB
};

static const int8_t s_dir_dx[4] = {1, 0, 1, 1};
static const int8_t s_dir_dy[4] = {0, 1, 1, -1};

/* Pattern4 -> 着法排序分 */
static const int32_t s_p4_score[PATTERN4_NB] = {
  0, 0, 4, 8, 16, 24, 48, 96, 256, 512, 1024, 2048, 8192, 65536
};

/* 逐格棋型状态：每格 4 方向的 Pattern2x（低4位黑/高4位白），及两方 Pattern4 */
CCMRAM static uint8_t s_pat_cell[GOMOKU_CELLS][4];
CCMRAM static uint8_t s_pat4[GOMOKU_CELLS][3];


/** @brief 某格沿 (dx,dy) 的密编码（中心视为空，4 格两翼）。 */
static uint32_t cell_line_key(int x, int y, int dx, int dy)
{
  uint32_t lo = 0;
  uint32_t hi = 0;
  int k;
  for (k = 1; k <= 4; ++k) {
    int nx = x - dx * k;
    int ny = y - dy * k;
    uint32_t c = 0U;
    if ((nx >= 0) && (nx < GOMOKU_N) && (ny >= 0) && (ny < GOMOKU_N)) {
      uint8_t v = s_cell[ny * GOMOKU_N + nx];
      c = (v == GOMOKU_EMPTY) ? 3U : ((v == GOMOKU_BLACK) ? 2U
                                                   : ((v == GOMOKU_WHITE) ? 1U : 0U));
    }
    lo |= c << (2 * (4 - k));
  }
  for (k = 1; k <= 4; ++k) {
    int nx = x + dx * k;
    int ny = y + dy * k;
    uint32_t c = 0U;
    if ((nx >= 0) && (nx < GOMOKU_N) && (ny >= 0) && (ny < GOMOKU_N)) {
      uint8_t v = s_cell[ny * GOMOKU_N + nx];
      c = (v == GOMOKU_EMPTY) ? 3U : ((v == GOMOKU_BLACK) ? 2U
                                                   : ((v == GOMOKU_WHITE) ? 1U : 0U));
    }
    hi |= c << (2 * (k - 1));
  }
  return (uint32_t)PAT2X_HALF_LO[lo] + (uint32_t)PAT2X_HALF_HI[hi];
}

/** @brief 刷新格 idx 在方向 d 上的 Pattern2x。 */
static void pat_cell_refresh(int idx, int d)
{
  int x = idx % GOMOKU_N;
  int y = idx / GOMOKU_N;
  s_pat_cell[idx][d] = PAT2X_TABLE[cell_line_key(x, y, s_dir_dx[d], s_dir_dy[d])];
}

/** @brief 由 4 方向 Pattern2x 重算格 idx 的两方 Pattern4。 */
static void pat4_refresh(int idx)
{
  int i0 = (int)(s_pat_cell[idx][0] & 0x0F);
  int i1 = (int)(s_pat_cell[idx][1] & 0x0F);
  int i2 = (int)(s_pat_cell[idx][2] & 0x0F);
  int i3 = (int)(s_pat_cell[idx][3] & 0x0F);
  s_pat4[idx][GOMOKU_BLACK] = PAT4_TABLE[((i0 * 16 + i1) * 16 + i2) * 16 + i3];
  i0 = (int)(s_pat_cell[idx][0] >> 4);
  i1 = (int)(s_pat_cell[idx][1] >> 4);
  i2 = (int)(s_pat_cell[idx][2] >> 4);
  i3 = (int)(s_pat_cell[idx][3] >> 4);
  s_pat4[idx][GOMOKU_WHITE] = PAT4_TABLE[((i0 * 16 + i1) * 16 + i2) * 16 + i3];
}

/** @brief 全盘重建逐格棋型（新局时调用）。 */
static void pat_refresh_all(void)
{
  int idx;
  int d;
  for (idx = 0; idx < GOMOKU_CELLS; ++idx) {
    for (d = 0; d < 4; ++d) {
      pat_cell_refresh(idx, d);
    }
    pat4_refresh(idx);
  }
}

/** @brief 落子/撤销后：只刷新经过 idx 的四条线上、距离<=4 的格的棋型。 */
static void cellpat_update(int idx)
{
  int x = idx % GOMOKU_N;
  int y = idx / GOMOKU_N;
  int d;
  for (d = 0; d < 4; ++d) {
    int sgn;
    for (sgn = -1; sgn <= 1; sgn += 2) {
      int k;
      for (k = 1; k <= 4; ++k) {
        int nx = x + sgn * k * (int)s_dir_dx[d];
        int ny = y + sgn * k * (int)s_dir_dy[d];
        int j;
        if ((nx < 0) || (nx >= GOMOKU_N) || (ny < 0) || (ny >= GOMOKU_N)) {
          break;
        }
        j = ny * GOMOKU_N + nx;
        pat_cell_refresh(j, d);
        pat4_refresh(j);
      }
    }
  }
}

/**
 * @brief 落子（内部）：更新增量分数、邻域计数、Zobrist 并写盘。
 * @param idx 格子索引（应为空）。
 * @param side 落子方。
 * @return 无。
 */
static void make_move(int idx, int side)
{
  uint8_t n = s_cell_win_n[idx];
  uint8_t i;

  s_cell[idx] = (uint8_t)side;
  bb_set(idx, side);
  for (i = 0U; i < n; ++i) {
    uint8_t *pc = &s_win_cnt[side][s_cell_win[idx][i]];
    if (*pc == 4U) {
      --s_four_win[side];
    }
    ++(*pc);
    if (*pc == 4U) {
      ++s_four_win[side];
    }
  }
  near_update(idx, 1);
  s_ctr[side] += s_ctr_tab[idx];
  s_hash ^= s_zob[side - 1][idx];
  pat_update(idx);
  cellpat_update(idx);
}

/**
 * @brief 撤销（内部）：与 make_move 对称。
 * @param idx 格子索引。
 * @param side 落子方。
 * @return 无。
 */
static void unmake_move(int idx, int side)
{
  uint8_t n = s_cell_win_n[idx];
  uint8_t i;

  for (i = 0U; i < n; ++i) {
    uint8_t *pc = &s_win_cnt[side][s_cell_win[idx][i]];
    if (*pc == 4U) {
      --s_four_win[side];
    }
    --(*pc);
    if (*pc == 4U) {
      ++s_four_win[side];
    }
  }
  s_cell[idx] = GOMOKU_EMPTY;
  bb_clear(idx, side);
  near_update(idx, -1);
  s_ctr[side] -= s_ctr_tab[idx];
  s_hash ^= s_zob[side - 1][idx];
  pat_update(idx);
  cellpat_update(idx);
}

int gomoku_side_at(int x, int y)
{
  if ((x < 0) || (x >= GOMOKU_N) || (y < 0) || (y >= GOMOKU_N)) {
    return -1;
  }
  return (int)s_cell[y * GOMOKU_N + x];
}

int gomoku_stone_count(void)
{
  return s_hist_n;
}

int gomoku_last_move(int *x, int *y)
{
  if (s_hist_n == 0) {
    return -1;
  }
  if (x != NULL) {
    *x = (int)(s_hist_cell[s_hist_n - 1] % GOMOKU_N);
  }
  if (y != NULL) {
    *y = (int)(s_hist_cell[s_hist_n - 1] / GOMOKU_N);
  }
  return 0;
}

/**
 * @brief 判断在 idx 落 side 后是否形成五连（idx 视为已落）。
 * @return 1 成五，0 否。
 */
/** @brief 置位/清位：把 idx 处 side 子登记到 4 个线位棋盘视图。 */
static void bb_set(int idx, int side)
{
  int x = idx % GOMOKU_N;
  int y = idx / GOMOKU_N;
  uint32_t bit = 1U << x;
  s_bb[side - 1][0][y] |= bit;
  s_bb[side - 1][1][x] |= (1U << y);
  s_bb[side - 1][2][x - y + 14] |= bit;
  s_bb[side - 1][3][x + y] |= bit;
}

static void bb_clear(int idx, int side)
{
  int x = idx % GOMOKU_N;
  int y = idx / GOMOKU_N;
  uint32_t bit = 1U << x;
  s_bb[side - 1][0][y] &= ~bit;
  s_bb[side - 1][1][x] &= ~(1U << y);
  s_bb[side - 1][2][x - y + 14] &= ~bit;
  s_bb[side - 1][3][x + y] &= ~bit;
}

/**
 * @brief 判断在 idx 落 side 后是否形成五连（idx 视为已落）。
 *        用 4 条经过该点的线位棋盘，SWAR 检测连续 5 位。
 */
static int makes_five(int idx, int side)
{
  int x = idx % GOMOKU_N;
  int y = idx / GOMOKU_N;
  uint32_t m;
  m = s_bb[side - 1][0][y];
  if ((m & (m >> 1) & (m >> 2) & (m >> 3) & (m >> 4)) != 0U) { return 1; }
  m = s_bb[side - 1][1][x];
  if ((m & (m >> 1) & (m >> 2) & (m >> 3) & (m >> 4)) != 0U) { return 1; }
  m = s_bb[side - 1][2][x - y + 14];
  if ((m & (m >> 1) & (m >> 2) & (m >> 3) & (m >> 4)) != 0U) { return 1; }
  m = s_bb[side - 1][3][x + y];
  if ((m & (m >> 1) & (m >> 2) & (m >> 3) & (m >> 4)) != 0U) { return 1; }
  return 0;
}

/** @brief 若 side 在 idx 落子是否成五（idx 当前为空，不改动棋盘）。 */
static int would_make_five(int idx, int side)
{
  int r;
  bb_set(idx, side);
  r = makes_five(idx, side);
  bb_clear(idx, side);
  return r;
}

/**
 * @brief 统计 side 的“成五点”数量（上限到 max）。
 * @return 数量（>=max 时提前返回）。
 */
static int count_five_points(int side, int max)
{
  int idx;
  int count = 0;

  for (idx = 0; idx < GOMOKU_CELLS; ++idx) {
    if ((s_cell[idx] != GOMOKU_EMPTY) || (s_near[idx] == 0U)) {
      continue;
    }
    if (would_make_five(idx, side) != 0) {
      if (++count >= max) {
        return count;
      }
    }
  }
  return count;
}

/** @brief 返回 side 第一个成五点，无则 -1。 */
static int find_five_point(int side)
{
  int idx;
  for (idx = 0; idx < GOMOKU_CELLS; ++idx) {
    if ((s_cell[idx] != GOMOKU_EMPTY) || (s_near[idx] == 0U)) {
      continue;
    }
    if (would_make_five(idx, side) != 0) {
      return idx;
    }
  }
  return -1;
}

int gomoku_place(int x, int y, int side)
{
  int idx;

  if ((x < 0) || (x >= GOMOKU_N) || (y < 0) || (y >= GOMOKU_N)) {
    return -1;
  }
  if ((side != GOMOKU_BLACK) && (side != GOMOKU_WHITE)) {
    return -1;
  }
  idx = y * GOMOKU_N + x;
  if (s_cell[idx] != GOMOKU_EMPTY) {
    return -1;
  }
  make_move(idx, side);
  s_hist_cell[s_hist_n] = (uint8_t)idx;
  s_hist_side[s_hist_n] = (uint8_t)side;
  ++s_hist_n;
  if (makes_five(idx, side) != 0) {
    s_winner = (uint8_t)side;
  }
  return 0;
}

int gomoku_undo(void)
{
  int idx;
  int side;

  if (s_hist_n == 0) {
    return -1;
  }
  --s_hist_n;
  idx = s_hist_cell[s_hist_n];
  side = s_hist_side[s_hist_n];
  unmake_move(idx, side);
  if (s_winner == (uint8_t)side) {
    s_winner = GOMOKU_EMPTY;
  }
  return 0;
}

int gomoku_status(void)
{
  if (s_winner != GOMOKU_EMPTY) {
    return (int)s_winner;
  }
  if (s_hist_n >= GOMOKU_CELLS) {
    return GOMOKU_DRAW;
  }
  return GOMOKU_EMPTY;
}

/* ------------------------------ 评估 ------------------------------ */

/** @brief 整盘静态评估（增量维护）。 */
/**
 * @brief 单个连子段（长度 len，两端开闭）的棋型分值。
 * @return 分值。
 */
__attribute__((always_inline)) static inline int32_t pattern_value(int len, int open_l, int open_r)
{
  if (len >= 5) {
    return 100000;
  }
  if (len == 4) {
    return (open_l && open_r) ? 8000 : ((open_l || open_r) ? 2000 : 0);
  }
  if (len == 3) {
    return (open_l && open_r) ? 1000 : ((open_l || open_r) ? 150 : 0);
  }
  if (len == 2) {
    return (open_l && open_r) ? 50 : ((open_l || open_r) ? 8 : 0);
  }
  if (len == 1) {
    return (open_l && open_r) ? 2 : 0;
  }
  return 0;
}

/**
 * @brief 局面评估（当前方视角，已钳位）。
 * @param side 当前方。
 * @return 分值。
 */
static int32_t eval_side(int side)
{
  int32_t v = (s_pat[side] - s_pat[3 - side]) + (s_ctr[side] - s_ctr[3 - side]);
  if (v > SCORE_EVAL_MAX) {
    v = SCORE_EVAL_MAX;
  } else if (v < -SCORE_EVAL_MAX) {
    v = -SCORE_EVAL_MAX;
  }
  return v;
}

/**
 * @brief 落点启发：一次遍历同时算出“进攻”和“防守”价值。
 * @param idx 候选点。
 * @param side 待走方。
 * @param off 输出进攻分（自己下这里的价值）。
 * @param def 输出防守分（对手下这里的价值）。
 * @return 无。
 */
static void move_heuristic(int idx, int side, int32_t *off, int32_t *def)
{
  int opp = 3 - side;
  int32_t o = 0;
  int32_t d = 0;
  uint8_t n = s_cell_win_n[idx];
  uint8_t i;

  for (i = 0U; i < n; ++i) {
    int w = (int)s_cell_win[idx][i];
    int mine = (int)s_win_cnt[side][w];
    int theirs = (int)s_win_cnt[opp][w];
    if (theirs == 0) {
      o += s_win_score[mine + 1];
    }
    if (mine == 0) {
      d += s_win_score[theirs + 1];
    }
  }
  *off = o;
  *def = d;
}

/* ------------------------------ 候选生成 ------------------------------ */

/**
 * @brief 生成本层候选点（已有邻子的空点），并按启发式分值降序排序。
 * @param ply 层号。
 * @param side 待走方。
 * @return 无。
 */
static void gen_candidates(int ply, int side)
{
  int n = 0;
  int idx;

  /* 强制着法：自己有成五点 -> 只走成五；否则对手有成五点 -> 只挡。 */
  {
    int w5 = find_five_point(side);
    if (w5 >= 0) {
      s_cand[ply][0] = (uint16_t)w5;
      s_cand_score[ply][0] = SCORE_WIN;
      s_ncand[ply] = 1;
      return;
    }
    w5 = find_five_point(3 - side);
    if (w5 >= 0) {
      s_cand[ply][0] = (uint16_t)w5;
      s_cand_score[ply][0] = SCORE_WIN / 2;
      s_ncand[ply] = 1;
      return;
    }
  }

  for (idx = 0; idx < GOMOKU_CELLS; ++idx) {
    int32_t off;
    int32_t def;
    if ((s_cell[idx] != GOMOKU_EMPTY) || (s_near[idx] == 0U)) {
      continue;
    }
    if (n >= MAX_CAND) {
      break;
    }
    move_heuristic(idx, side, &off, &def);
    s_cand[ply][n] = (uint16_t)idx;
    s_cand_score[ply][n] = (s_p4_score[s_pat4[idx][side]]
                            + s_p4_score[s_pat4[idx][3 - side]]) * 64
                           + off * 2 + def;
    ++n;
  }

  if (n == 0) {
    int c = (GOMOKU_N / 2) * GOMOKU_N + (GOMOKU_N / 2);
    s_cand[ply][0] = (uint16_t)c;
    s_cand_score[ply][0] = 0;
    n = 1;
  }

  {
    int i;
    for (i = 1; i < n; ++i) {
      uint16_t ci = s_cand[ply][i];
      int32_t si = s_cand_score[ply][i];
      int j = i - 1;
      while ((j >= 0) && (s_cand_score[ply][j] < si)) {
        s_cand[ply][j + 1] = s_cand[ply][j];
        s_cand_score[ply][j + 1] = s_cand_score[ply][j];
        --j;
      }
      s_cand[ply][j + 1] = ci;
      s_cand_score[ply][j + 1] = si;
    }
  }
  s_ncand[ply] = n;
}

/* ------------------------------ 置换表 ------------------------------ */

static void tt_store(uint32_t key, int depth, int score, int flag, int move)
{
  tt_entry_t *e = &s_tt[key & TT_MASK];
  if ((e->key == key) && ((int)(e->meta >> 2) > depth) && (e->move != 255U)) {
    return; /* 已有更深结果 */
  }
  e->key = key;
  e->score = (int16_t)score;
  e->move = (uint8_t)((move < 0) ? 255 : move);
  e->meta = (uint8_t)(((depth & 0x3F) << 2) | (flag & 3));
}

/** @brief 从置换表取移动与分数；返回是否命中。 */
static int tt_probe(uint32_t key, int depth, int alpha, int beta, int ply,
                    int *out_score, int *out_move)
{
  const tt_entry_t *e = &s_tt[key & TT_MASK];
  int score;
  int flag;

  *out_move = -1;
  if (e->key != key) {
    return 0;
  }
  if (e->move != 255U) {
    *out_move = (int)e->move;
  }
  if ((int)(e->meta >> 2) < depth) {
    return 0;
  }
  flag = e->meta & 3;
  score = e->score;
  if ((score >= SCORE_MATE) || (score <= -SCORE_MATE)) {
    return 0; /* 不信任 TT 中的胜负分 */
  }
  (void)ply;
  *out_score = score;
  if (flag == 0) {
    return 1;
  }
  if ((flag == 1) && (score >= beta)) {
    return 1;
  }
  if ((flag == 2) && (score <= alpha)) {
    return 1;
  }
  return 0;
}

static int negamax(int side, int depth, int alpha, int beta, int ply)
{
  int opp = 3 - side;
  int limit;
  int i;
  int best = -SCORE_INF;
  int best_move = -1;
  int tt_move = -1;
  int tt_score = 0;
  int alpha0 = alpha;
  uint32_t key;

  ++s_nodes;
  s_pv_len[ply] = 0U;
  if ((s_nodes & 0x3FFU) == 0U) {
    if ((s_stop_hook != NULL) && (s_stop_hook() != 0)) {
      s_abort = 1;
      return 0;
    }
    if ((s_time_limited != 0) &&
        ((int32_t)(DWT->CYCCNT - s_deadline) >= 0)) {
      s_abort = 1;
      return 0;
    }
  }

  key = s_hash;
  if (tt_probe(key, depth, alpha, beta, ply, &tt_score, &tt_move) != 0) {
    return tt_score;
  }

  if (depth <= 0) {
    return (int)eval_side(side);
  }

  gen_candidates(ply, side);
  if (s_ncand[ply] == 0) {
    return 0;
  }
  /* 命中置换表 -> 把该着法提到最前。 */
  if (tt_move >= 0) {
    for (i = 1; i < s_ncand[ply]; ++i) {
      if (s_cand[ply][i] == (uint16_t)tt_move) {
        uint16_t t = s_cand[ply][0];
        s_cand[ply][0] = s_cand[ply][i];
        s_cand[ply][i] = t;
        break;
      }
    }
  }
  limit = s_ncand[ply];
  if (limit > NODE_CAND) {
    limit = NODE_CAND;
  }

  for (i = 0; i < limit; ++i) {
    int idx = s_cand[ply][i];
    int score;
    int full = (i == 0);

    make_move(idx, side);
    if (makes_five(idx, side) != 0) {
      unmake_move(idx, side);
      return SCORE_WIN - ply;
    }
    if (full) {
      score = -negamax(opp, depth - 1, -beta, -alpha, ply + 1);
    } else {
      /* PVS：先零窗试探，失败再全窗重搜。 */
      score = -negamax(opp, depth - 1, -alpha - 1, -alpha, ply + 1);
      if ((s_abort == 0) && (score > alpha) && (score < beta)) {
        score = -negamax(opp, depth - 1, -beta, -alpha, ply + 1);
      }
    }
    unmake_move(idx, side);

    if (s_abort != 0) {
      return 0;
    }
    if (score > best) {
      best = score;
      best_move = idx;
      s_pv[ply][0] = (uint8_t)idx;
      if (s_pv_len[ply + 1] > 0U) {
        memcpy(&s_pv[ply][1], &s_pv[ply + 1][0], (size_t)s_pv_len[ply + 1]);
      }
      s_pv_len[ply] = (uint8_t)(s_pv_len[ply + 1] + 1U);
    }
    if (score > alpha) {
      alpha = score;
    }
    if (alpha >= beta) {
      if (s_killer[ply][0] != idx) {
        s_killer[ply][1] = s_killer[ply][0];
        s_killer[ply][0] = (uint16_t)idx;
      }
      break;
    }
  }

  if (s_abort == 0) {
    int flag = (best <= alpha0) ? 2 : ((best >= beta) ? 1 : 0);
    int store = best;
    if (store >= SCORE_MATE) {
      store += ply;
    } else if (store <= -SCORE_MATE) {
      store -= ply;
    }
    tt_store(key, depth, store, flag, best_move);
  }
  return best;
}

/* ------------------------------ VCF（连四威胁） ------------------------------ */

/**
 * @brief VCF 候选：邻子空点，按启发式降序。
 */
static void vcf_gen(int vd, int side)
{
  int n = 0;
  int idx;

  for (idx = 0; idx < GOMOKU_CELLS; ++idx) {
    int32_t off;
    int32_t def;
    if ((s_cell[idx] != GOMOKU_EMPTY) || (s_near[idx] == 0U)) {
      continue;
    }
    if (n >= VCF_MAX_CAND) {
      break;
    }
    move_heuristic(idx, side, &off, &def);
    s_vcf_cand[vd][n] = (uint16_t)idx;
    ++n;
  }
  s_vcf_n[vd] = n;
}

/**
 * @brief 连续冲四搜索：side 走，是否能强制取胜。
 * @param side 进攻方。
 * @param depth 还能连续冲四的手数。
 * @param first 顶层命中时输出第一步（可为 NULL）。
 * @return 1 必胜，0 未找到。
 */
static int vcf(int side, int depth, int *first)
{
  /* 节点上限 + 时限/中断检查：VCF 可能很深/爆炸，必须可被截断。 */
  if (++s_vcf_nodes > VCF_NODE_LIMIT) {
    return 0;
  }
  if ((s_stop_hook != NULL) && (s_stop_hook() != 0)) {
    return 0;
  }
  if ((s_time_limited != 0) &&
      ((int32_t)(DWT->CYCCNT - s_deadline) >= 0)) {
    return 0;
  }

  int opp = 3 - side;
  int i;
  int n;
  int vd = depth;

  if (depth <= 0) {
    return 0;
  }
  if (vd >= VCF_MAX_PLY) {
    vd = VCF_MAX_PLY - 1;
  }

  vcf_gen(vd, side);
  n = s_vcf_n[vd];
  for (i = 0; i < n; ++i) {
    int m = s_vcf_cand[vd][i];
    int w;
    int b;

    make_move(m, side);
    if (makes_five(m, side) != 0) {
      unmake_move(m, side);
      if (first != NULL) {
        *first = m;
      }
      return 1;
    }
    w = count_five_points(side, 2);
    if (w == 0) {
      unmake_move(m, side);
      continue; /* 不是冲四 */
    }
    if (count_five_points(opp, 1) > 0) {
      unmake_move(m, side);
      continue; /* 对手能先成五 -> 失败 */
    }
    if (w >= 2) {
      unmake_move(m, side);
      if (first != NULL) {
        *first = m;
      }
      return 1; /* 双四 */
    }
    b = find_five_point(side);
    if (b < 0) {
      unmake_move(m, side);
      continue;
    }
    make_move(b, opp); /* 对手被迫挡 */
    if (vcf(side, depth - 1, NULL) != 0) {
      unmake_move(b, opp);
      unmake_move(m, side);
      if (first != NULL) {
        *first = m;
      }
      return 1;
    }
    unmake_move(b, opp);
    unmake_move(m, side);
  }
  return 0;
}

/* ------------------------------ 根搜索 ------------------------------ */

/**
 * @brief 根节点搜索：返回最佳着法与评分。
 */
/* ------------------------------ VCT（连续威胁：四 + 活三） ------------------------------ */

/** @brief VCT 可打断检查（预算/时限/中断钩子）。 */
static int vct_abort(void)
{
  if (++s_vct_nodes > VCT_NODE_LIMIT) {
    return 1;
  }
  if ((s_stop_hook != NULL) && (s_stop_hook() != 0)) {
    return 1;
  }
  if ((s_time_limited != 0) &&
      ((int32_t)(DWT->CYCCNT - s_deadline) >= 0)) {
    return 1;
  }
  return 0;
}

/** @brief 由 DEFENCE 表收集我方在 m 落子后各线上的防守点。 */
static int vct_defences(int m, int side, int *out, int max)
{
  int x = m % GOMOKU_N;
  int y = m / GOMOKU_N;
  int att = (side == GOMOKU_BLACK) ? 0 : 1;
  int n = 0;
  int d;

  for (d = 0; d < 4; ++d) {
    int dx = s_dir_dx[d];
    int dy = s_dir_dy[d];
    uint32_t lo = 0;
    uint32_t hi = 0;
    uint8_t mask;
    int k;
    int i;

    for (k = 1; k <= 4; ++k) {
      int nx = x - dx * k;
      int ny = y - dy * k;
      uint32_t c = 0U;
      if ((nx >= 0) && (nx < GOMOKU_N) && (ny >= 0) && (ny < GOMOKU_N)) {
        {
          uint8_t cv = s_cell[ny * GOMOKU_N + nx];
          c = (cv == GOMOKU_EMPTY) ? 3U : ((cv == GOMOKU_BLACK) ? 2U
                                                       : ((cv == GOMOKU_WHITE) ? 1U : 0U));
        }
      }
      lo |= c << (2 * (4 - k));
    }
    for (k = 1; k <= 4; ++k) {
      int nx = x + dx * k;
      int ny = y + dy * k;
      uint32_t c = 0U;
      if ((nx >= 0) && (nx < GOMOKU_N) && (ny >= 0) && (ny < GOMOKU_N)) {
        {
          uint8_t cv = s_cell[ny * GOMOKU_N + nx];
          c = (cv == GOMOKU_EMPTY) ? 3U : ((cv == GOMOKU_BLACK) ? 2U
                                                       : ((cv == GOMOKU_WHITE) ? 1U : 0U));
        }
      }
      hi |= c << (2 * (k - 1));
    }
    mask = DEFENCE_TABLE[(uint32_t)PAT2X_HALF_LO[lo] + (uint32_t)PAT2X_HALF_HI[hi]][att];
    for (i = 0; i < 8; ++i) {
      if ((mask & (uint8_t)(1U << i)) != 0U) {
        int sgn = (i < 4) ? -1 : 1;
        int dist = (i < 4) ? (4 - i) : (i - 3);
        int nx = x + sgn * dx * dist;
        int ny = y + sgn * dy * dist;
        if ((nx >= 0) && (nx < GOMOKU_N) && (ny >= 0) && (ny < GOMOKU_N) &&
            (s_cell[ny * GOMOKU_N + nx] == GOMOKU_EMPTY)) {
          int j = ny * GOMOKU_N + nx;
          int dup = 0;
          int t;
          for (t = 0; t < n; ++t) {
            if (out[t] == j) {
              dup = 1;
              break;
            }
          }
          if ((dup == 0) && (n < max)) {
            out[n] = j;
            ++n;
          }
        }
      }
    }
  }
  return n;
}

/**
 * @brief 连续威胁搜索(VCT)：只走能造四或活三的着法。
 *  - 我方造四 -> 对手被迫堵五点后继续
 *  - 我方活三 -> 对手防守点查 DEFENCE 表，另加对手反杀点；全部堵死才算此路失败
 * @return 1 必胜，0 未找到。
 */
static int vct(int side, int depth, int *first)
{
  int opp = 3 - side;
  int cand[MAX_PLY];
  int n = 0;
  int idx;
  int i;

  if (depth <= 0) {
    return 0;
  }
  if (vct_abort() != 0) {
    return 0;
  }
  if (count_five_points(side, 1) > 0) {
    return 1;
  }

  for (idx = 0; idx < GOMOKU_CELLS; ++idx) {
    if ((s_cell[idx] != GOMOKU_EMPTY) || (s_near[idx] == 0U)) {
      continue;
    }
    if ((int)s_pat4[idx][side] >= P4_H_FLEX3) {
      if (n < MAX_PLY) {
        cand[n] = idx;
        ++n;
      }
    }
  }
  for (i = 1; i < n; ++i) {
    int ci = cand[i];
    int pi = (int)s_pat4[ci][side];
    int j = i - 1;
    while ((j >= 0) && ((int)s_pat4[cand[j]][side] < pi)) {
      cand[j + 1] = cand[j];
      --j;
    }
    cand[j + 1] = ci;
  }

  for (i = 0; i < n; ++i) {
    int c = cand[i];
    int fp;

    make_move(c, side);
    fp = count_five_points(side, 2);
    if (fp >= 2) {
      unmake_move(c, side);
      if (first != NULL) {
        *first = c;
      }
      return 1; /* 活四/双四 */
    }
    if (fp == 1) {
      if (count_five_points(opp, 1) == 0) {
        int b = find_five_point(side);
        if (b >= 0) {
          make_move(b, opp);
          if ((count_five_points(opp, 1) == 0) &&
              (vct(side, depth - 1, NULL) != 0)) {
            unmake_move(b, opp);
            unmake_move(c, side);
            if (first != NULL) {
              *first = c;
            }
            return 1;
          }
          unmake_move(b, opp);
        }
      }
    } else {
      int defs[24];
      int ndef = vct_defences(c, side, defs, 24);
      int allfail;
      int tried = 0;
      int t;

      /* 追加对手反杀点(能造四 -> 形成五威胁) */
      for (idx = 0; (idx < GOMOKU_CELLS) && (ndef < 24); ++idx) {
        int j;
        int dup = 0;
        if ((s_cell[idx] != GOMOKU_EMPTY) || (s_near[idx] == 0U) ||
            ((int)s_pat4[idx][opp] < P4_E_BLOCK4)) {
          continue;
        }
        for (j = 0; j < ndef; ++j) {
          if (defs[j] == idx) {
            dup = 1;
            break;
          }
        }
        if (dup == 0) {
          defs[ndef] = idx;
          ++ndef;
        }
      }

      allfail = (ndef > 0) ? 1 : 0;
      if (count_five_points(opp, 1) > 0) {
        allfail = 0; /* 对手能先成五 -> 必须回防 */
      }
      for (t = 0; (t < ndef) && (allfail != 0); ++t) {
        make_move(defs[t], opp);
        if (count_five_points(opp, 1) > 0) {
          allfail = 0;
        } else if (vct(side, depth - 1, NULL) == 0) {
          allfail = 0;
        }
        unmake_move(defs[t], opp);
        ++tried;
      }
      if ((allfail != 0) && (tried > 0)) {
        unmake_move(c, side);
        if (first != NULL) {
          *first = c;
        }
        return 1;
      }
    }
    unmake_move(c, side);
  }
  return 0;
}

static void search_root(int side, int depth, int alpha, int beta,
                        int *best_idx, int *out_score)
{
  int opp = 3 - side;
  int best = -SCORE_INF;
  int best_move = -1;
  int limit;
  int i;
  uint32_t key = s_hash;

  s_abort = 0;
  s_pv_len[0] = 0U;
  gen_candidates(0, side);
  limit = s_ncand[0];
  if (limit > ROOT_CAND) {
    limit = ROOT_CAND;
  }
  /* 置换表着法优先。 */
  {
    int tt_move = -1;
    int dummy = 0;
    (void)tt_probe(key, 0, -SCORE_INF, SCORE_INF, 0, &dummy, &tt_move);
    if (tt_move >= 0) {
      for (i = 1; i < limit; ++i) {
        if (s_cand[0][i] == (uint16_t)tt_move) {
          uint16_t t = s_cand[0][0];
          s_cand[0][0] = s_cand[0][i];
          s_cand[0][i] = t;
          break;
        }
      }
    }
  }

  for (i = 0; i < limit; ++i) {
    int idx = s_cand[0][i];
    int score;

    make_move(idx, side);
    if (makes_five(idx, side) != 0) {
      unmake_move(idx, side);
      *best_idx = idx;
      *out_score = SCORE_WIN - 1;
      return;
    }
    score = -negamax(opp, depth - 1, -beta, -alpha, 1);
    unmake_move(idx, side);

    if (s_abort != 0) {
      break;
    }
    if (score > best) {
      best = score;
      best_move = idx;
      s_pv[0][0] = (uint8_t)idx;
      if (s_pv_len[1] > 0U) {
        memcpy(&s_pv[0][1], &s_pv[1][0], (size_t)s_pv_len[1]);
      }
      s_pv_len[0] = (uint8_t)(s_pv_len[1] + 1U);
    }
    if (score > alpha) {
      alpha = score;
    }
  }

  if (best_move < 0) {
    best_move = s_cand[0][0];
    best = 0;
  }
  *best_idx = best_move;
  *out_score = best;
}

int gomoku_search(int side, int max_depth, uint32_t time_limit_ms,
                  gomoku_result_t *res)
{
  int depth;
  int best_idx = -1;
  int best_score = 0;
  uint32_t t0 = HAL_GetTick();
  int reached = 0;
  int vcf_move = -1;

  if ((side != GOMOKU_BLACK) && (side != GOMOKU_WHITE)) {
    return -1;
  }
  if (max_depth < 1) {
    max_depth = 1;
  }
  if (max_depth > MAX_PLY) {
    max_depth = MAX_PLY;
  }
  if (s_hist_n == 0) {
    int c = (GOMOKU_N / 2) * GOMOKU_N + (GOMOKU_N / 2);
    if (res != NULL) {
      res->x = c % GOMOKU_N;
      res->y = c / GOMOKU_N;
      res->score = 0;
      res->depth = 0;
      res->nodes = 0;
      res->time_ms = 0;
    }
    return 0;
  }

  s_nodes = 0U;
  s_root_pv_len = 0U;
  s_time_limited = (time_limit_ms > 0U) ? 1 : 0;
  s_deadline = DWT->CYCCNT +
               (uint32_t)((uint64_t)time_limit_ms * 170000ULL);
  s_abort = 0;
  memset(s_killer, 0xFF, sizeof(s_killer));

  /* 1) 能成五直接赢 */
  {
    int w = find_five_point(side);
    if (w >= 0) {
      best_idx = w;
      best_score = SCORE_WIN - 1;
      goto done;
    }
  }
  /* 2) 对手成五点：先挡住（搜索也会找到，这里保证浅层不错） */
  {
    int w = find_five_point(3 - side);
    if (w >= 0) {
      best_idx = w;
      best_score = 0;
      goto done;
    }
  }
  /* 3) VCF/VCT：连续威胁必杀搜索。给它们单独的小预算(时限的 1/4，上限 800ms)，
     否则可能吃掉整个时限，导致主搜索只到很浅的深度。 */
  {
    uint32_t full_deadline = s_deadline;
    s_vcf_nodes = 0U;
    if (time_limit_ms > 0U) {
      uint32_t budget = (uint32_t)(((uint64_t)time_limit_ms * 170000ULL) / 4ULL);
      if (budget > 136000000U) { /* 800ms */
        budget = 136000000U;
      }
      s_deadline = DWT->CYCCNT + budget;
    }
    if (vcf(side, VCF_MAX_PLY, &vcf_move) != 0 && vcf_move >= 0) {
      s_deadline = full_deadline;
      best_idx = vcf_move;
      best_score = SCORE_WIN - 2;
      goto done;
    }
    s_vct_nodes = 0U;
    if (vct(side, VCF_MAX_PLY, &vcf_move) != 0 && vcf_move >= 0) {
      s_deadline = full_deadline;
      best_idx = vcf_move;
      best_score = SCORE_WIN - 3;
      goto done;
    }
    s_deadline = full_deadline;
  }

  for (depth = 1; depth <= max_depth; ++depth) {
    int idx = -1;
    int score = 0;
    int alpha = -SCORE_INF;
    int beta = SCORE_INF;

    /* 迭代加深 + 轻度 aspiration。 */
    if ((depth >= 4) && (reached >= 3)) {
      alpha = best_score - 200;
      beta = best_score + 200;
    }
    {
      int retries = 0;
      for (;;) {
        search_root(side, depth, alpha, beta, &idx, &score);
        if (s_abort != 0) {
          break;
        }
        if ((score <= alpha) || (score >= beta)) {
          /* 失败后重搜；最多 2 次，之后退化为全窗口，保证一定终止。 */
          if (++retries >= 3) {
            alpha = -SCORE_INF;
            beta = SCORE_INF;
          } else if (score <= alpha) {
            alpha = -SCORE_INF;
            beta = score + 1;
          } else {
            beta = SCORE_INF;
            alpha = score - 1;
          }
          continue;
        }
        break;
      }
    }
    if (s_abort != 0) {
      break;
    }
    best_idx = idx;
    best_score = score;
    reached = depth;
    if ((score >= SCORE_MATE) || (score <= -SCORE_MATE)) {
      break;
    }
    if (s_ncand[0] <= 1) {
      break;
    }
    if ((s_time_limited != 0) && ((int32_t)(DWT->CYCCNT - s_deadline) >= 0)) {
      break;
    }
  }

  s_root_pv_len = s_pv_len[0];
  if (s_root_pv_len > 0U) {
    memcpy(s_root_pv, &s_pv[0][0], (size_t)s_root_pv_len);
  }

done:
  if (best_idx < 0) {
    int i;
    for (i = 0; i < GOMOKU_CELLS; ++i) {
      if (s_cell[i] == GOMOKU_EMPTY) {
        best_idx = i;
        break;
      }
    }
  }
  if (best_idx < 0) {
    return -1;
  }
  if (s_root_pv_len == 0U) {
    s_root_pv[0] = (uint8_t)best_idx; /* 短路(成五/封堵/VCF)时给单步线路 */
    s_root_pv_len = 1U;
  }
  if (res != NULL) {
    res->x = best_idx % GOMOKU_N;
    res->y = best_idx / GOMOKU_N;
    res->score = best_score;
    res->depth = reached;
    res->nodes = s_nodes;
    res->time_ms = HAL_GetTick() - t0;
  }
  return 0;
}

int gomoku_think(int side, int max_depth, uint32_t time_limit_ms,
                 gomoku_result_t *res)
{
  gomoku_result_t r;

  if (gomoku_search(side, max_depth, time_limit_ms, &r) != 0) {
    return -1;
  }
  if (gomoku_place(r.x, r.y, side) != 0) {
    return -1;
  }
  if (res != NULL) {
    *res = r;
  }
  return 0;
}

void gomoku_set_stop_hook(gomoku_stop_fn fn)
{
  s_stop_hook = fn;
}

int gomoku_ponder(int side, uint32_t slice_ms, int predict)
{
  gomoku_result_t r;

  if ((side != GOMOKU_BLACK) && (side != GOMOKU_WHITE)) {
    return -1;
  }
  if (gomoku_status() != GOMOKU_EMPTY) {
    return -1;
  }
  if (gomoku_search(side, MAX_PLY, slice_ms, &r) != 0) {
    return -1;
  }
  if ((predict == 0) || (r.depth == 0)) {
    return 0;
  }
  /* 预测 side 的最佳着法，并对“对方应手”做一次搜索预热置换表。
     仅 make/unmake，不改变历史与胜负状态。 */
  {
    int idx = r.y * GOMOKU_N + r.x;
    if ((idx >= 0) && (idx < GOMOKU_CELLS) && (s_cell[idx] == GOMOKU_EMPTY)) {
      make_move(idx, side);
      (void)gomoku_search(3 - side, MAX_PLY, slice_ms, NULL);
      unmake_move(idx, side);
    }
  }
  return 0;
}

void gomoku_get_pv(uint8_t *out, int *len)
{
  int n = (int)s_root_pv_len;
  if (n > MAX_PLY) {
    n = MAX_PLY;
  }
  if (out != NULL) {
    memcpy(out, s_root_pv, (size_t)n);
  }
  if (len != NULL) {
    *len = n;
  }
}

int gomoku_to_text(char *buf, int cap)
{
  int pos = 0;
  int y;

  if ((buf == NULL) || (cap < 64)) {
    return 0;
  }
  for (y = GOMOKU_N - 1; y >= 0; --y) {
    int x;
    pos += snprintf(&buf[pos], (size_t)(cap - pos), "%2d ", y + 1);
    for (x = 0; x < GOMOKU_N; ++x) {
      uint8_t v = s_cell[y * GOMOKU_N + x];
      char ch = '.';
      if (v == GOMOKU_BLACK) {
        ch = 'X';
      } else if (v == GOMOKU_WHITE) {
        ch = 'O';
      }
      if (pos < (cap - 1)) {
        buf[pos++] = ch;
        buf[pos++] = ' ';
      }
    }
    if (pos < (cap - 1)) {
      buf[pos++] = '\r';
      buf[pos++] = '\n';
    }
  }
  pos += snprintf(&buf[pos], (size_t)(cap - pos),
                  "   A B C D E F G H I J K L M N O\r\n");
  return pos;
}
