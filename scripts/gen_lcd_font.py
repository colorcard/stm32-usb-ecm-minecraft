#!/usr/bin/env python3
"""生成 LCD 用的 5x7 点阵字库（ASCII 32..90）。

字形用 7 行 x 5 列的 '#'/'.' 描述，便于人工校对；输出 C 头文件。
用法: python3 scripts/gen_lcd_font.py [输出路径]
默认输出 project/code/lcd_font5x7.h
"""

import os
import sys

G = {
    ' ': ["     "] * 7,
    '-': ["     ", "     ", "     ", "#####", "     ", "     ", "     "],
    '.': ["     ", "     ", "     ", "     ", "     ", ".##..", ".##.."],
    '/': ["....#", "....#", "...##", "..##.", ".##..", "##...", "#...."],
    '0': [" ### ", "#   #", "#  ##", "# # #", "##  #", "#   #", " ### "],
    '1': ["  #  ", " ##  ", "  #  ", "  #  ", "  #  ", "  #  ", " ### "],
    '2': [" ### ", "#   #", "    #", "   # ", "  #  ", " #   ", "#####"],
    '3': ["#####", "   # ", "  #  ", "   # ", "    #", "#   #", " ### "],
    '4': ["   # ", "  ## ", " # # ", "#  # ", "#####", "   # ", "   # "],
    '5': ["#####", "#    ", "#### ", "    #", "    #", "#   #", " ### "],
    '6': ["  ## ", " #   ", "#    ", "#### ", "#   #", "#   #", " ### "],
    '7': ["#####", "    #", "   # ", "  #  ", " #   ", " #   ", " #   "],
    '8': [" ### ", "#   #", "#   #", " ### ", "#   #", "#   #", " ### "],
    '9': [" ### ", "#   #", "#   #", " ####", "    #", "   # ", " ##  "],
    ':': ["     ", " ##  ", " ##  ", "     ", " ##  ", " ##  ", "     "],
    'A': [" ### ", "#   #", "#   #", "#####", "#   #", "#   #", "#   #"],
    'B': ["#### ", "#   #", "#   #", "#### ", "#   #", "#   #", "#### "],
    'C': [" ### ", "#   #", "#    ", "#    ", "#    ", "#   #", " ### "],
    'D': ["#### ", "#   #", "#   #", "#   #", "#   #", "#   #", "#### "],
    'E': ["#####", "#    ", "#    ", "#### ", "#    ", "#    ", "#####"],
    'F': ["#####", "#    ", "#    ", "#### ", "#    ", "#    ", "#    "],
    'G': [" ### ", "#   #", "#    ", "# ###", "#   #", "#   #", " ####"],
    'H': ["#   #", "#   #", "#   #", "#####", "#   #", "#   #", "#   #"],
    'I': [" ### ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", " ### "],
    'J': ["  ###", "   # ", "   # ", "   # ", "   # ", "#  # ", " ##  "],
    'K': ["#   #", "#  # ", "# #  ", "##   ", "# #  ", "#  # ", "#   #"],
    'L': ["#    ", "#    ", "#    ", "#    ", "#    ", "#    ", "#####"],
    'M': ["#   #", "## ##", "# # #", "# # #", "#   #", "#   #", "#   #"],
    'N': ["#   #", "##  #", "# # #", "# # #", "#  ##", "#   #", "#   #"],
    'O': [" ### ", "#   #", "#   #", "#   #", "#   #", "#   #", " ### "],
    'P': ["#### ", "#   #", "#   #", "#### ", "#    ", "#    ", "#    "],
    'Q': [" ### ", "#   #", "#   #", "#   #", "# # #", "#  # ", " ## #"],
    'R': ["#### ", "#   #", "#   #", "#### ", "# #  ", "#  # ", "#   #"],
    'S': [" ####", "#    ", "#    ", " ### ", "    #", "    #", "#### "],
    'T': ["#####", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  "],
    'U': ["#   #", "#   #", "#   #", "#   #", "#   #", "#   #", " ### "],
    'V': ["#   #", "#   #", "#   #", "#   #", "#   #", " # # ", "  #  "],
    'W': ["#   #", "#   #", "#   #", "# # #", "# # #", "## ##", "#   #"],
    'X': ["#   #", "#   #", " # # ", "  #  ", " # # ", "#   #", "#   #"],
    'Y': ["#   #", "#   #", " # # ", "  #  ", "  #  ", "  #  ", "  #  "],
    'Z': ["#####", "    #", "   # ", "  #  ", " #   ", "#    ", "#####"],
}

FIRST, LAST = 32, 90


def rows_to_bytes(rows):
    out = []
    for r in rows:
        v = 0
        for i, c in enumerate(r):
            if c == '#':
                v |= 1 << (4 - i)  # 位4为最左像素
        out.append(v)
    return out


def main():
    data = []
    for code in range(FIRST, LAST + 1):
        ch = chr(code)
        rows = G.get(ch)
        if rows is None:
            data.append([0] * 7)
        else:
            assert len(rows) == 7 and all(len(r) == 5 for r in rows), ch
            data.append(rows_to_bytes(rows))

    out_path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        "project", "code", "lcd_font5x7.h")
    lines = []
    lines.append("/**")
    lines.append(" * @file lcd_font5x7.h")
    lines.append(" * @brief 5x7 点阵字库（ASCII %d..%d），由 scripts/gen_lcd_font.py 生成。"
                 % (FIRST, LAST))
    lines.append(" * @note 每字符 7 行，每行低 5 位有效（bit4 在左）。")
    lines.append(" */")
    lines.append("#ifndef _LCD_FONT5X7_H_")
    lines.append("#define _LCD_FONT5X7_H_")
    lines.append("")
    lines.append("#include <stdint.h>")
    lines.append("")
    lines.append("#define LCD_FONT_FIRST  %dU" % FIRST)
    lines.append("#define LCD_FONT_LAST   %dU" % LAST)
    lines.append("#define LCD_FONT_WIDTH  5U")
    lines.append("#define LCD_FONT_HEIGHT 7U")
    lines.append("")
    lines.append("static const uint8_t s_lcd_font[%d][7] = {" % len(data))
    for idx, g in enumerate(data):
        code = FIRST + idx
        ch = chr(code)
        disp = "' '" if ch == " " else ch
        lines.append("  {%s}, /* %d %s */" % (", ".join("0x%02X" % b for b in g),
                                              code, disp))
    lines.append("};")
    lines.append("")
    lines.append("#endif /* _LCD_FONT5X7_H_ */")
    lines.append("")

    with open(out_path, "w") as f:
        f.write("\n".join(lines))
    print("wrote", out_path)


if __name__ == "__main__":
    main()
