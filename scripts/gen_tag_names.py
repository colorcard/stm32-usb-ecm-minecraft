#!/usr/bin/env python3
"""从客户端 jar 的 data/minecraft/tags/ 提取全部标签名，生成 C 头文件。

以“空标签”下发（标签存在即可，用于满足客户端对注册表数据的标签引用）。
用法: python3 scripts/gen_tag_names.py <client.jar> [reg.txt] [输出]
"""

import os
import re
import sys
import zipfile

DEFAULT_OUT = os.path.join("project", "code", "mc_tag_names.h")


def load_registry_names(reg_txt_path):
    names = set()
    if not os.path.exists(reg_txt_path):
        return names
    text = open(reg_txt_path, encoding="utf-8", errors="ignore").read()
    for m in re.finditer(r"<code>minecraft:([a-z0-9_/]+)</code>", text):
        names.add(m.group(1))
    return names


def main():
    jar = sys.argv[1]
    reg_txt = sys.argv[2] if len(sys.argv) > 2 else "refer/reg.txt"
    out_path = sys.argv[3] if len(sys.argv) > 3 else DEFAULT_OUT

    regs = load_registry_names(reg_txt)
    # 同步注册表（可能不在 reg.txt 中时补上）
    regs.update([
        "worldgen/biome", "dimension_type", "chat_type", "trim_pattern",
        "trim_material", "wolf_variant", "wolf_sound_variant", "pig_variant",
        "pig_sound_variant", "frog_variant", "cat_variant", "cat_sound_variant",
        "cow_variant", "cow_sound_variant", "chicken_variant",
        "chicken_sound_variant", "zombie_nautilus_variant", "painting_variant",
        "sulfur_cube_archetype", "damage_type", "banner_pattern", "enchantment",
        "jukebox_song", "instrument", "test_environment", "test_instance",
        "dialog", "world_clock", "timeline", "decorated_pot_pattern",
        "block_transformer", "block_state_provider",
    ])
    regs = sorted(regs, key=len, reverse=True)

    zf = zipfile.ZipFile(jar)
    prefix = "data/minecraft/tags/"
    groups = {}
    for n in zf.namelist():
        if not (n.startswith(prefix) and n.endswith(".json")):
            continue
        p = n[len(prefix):-len(".json")]
        reg = None
        for r in regs:
            if p == r or p.startswith(r + "/"):
                reg = r
                break
        if reg is None:
            continue
        tag = p[len(reg) + 1:]
        groups.setdefault(reg, []).append(tag)

    # 只保留“同步注册表”的标签：内置注册表（block/item/...）的标签客户端自带
    # （MC-249007 保留），无需下发；且它们的标签组很大，单包会超出发送缓冲。
    SYNCED = {
        "banner_pattern", "damage_type", "dialog", "enchantment", "instrument",
        "painting_variant", "timeline", "trim_material",
        "trim_pattern", "jukebox_song", "chat_type", "wolf_variant",
        "cat_variant", "frog_variant", "pig_variant", "cow_variant",
        "chicken_variant", "dimension_type", "test_environment",
        "test_instance", "world_clock", "decorated_pot_pattern",
        "sulfur_cube_archetype", "block_transformer", "block_state_provider",
    }
    groups = {k: v for k, v in groups.items() if k in SYNCED}

    lines = []
    lines.append("/**")
    lines.append(" * @file mc_tag_names.h")
    lines.append(" * @brief 原版标签名列表（以空标签下发，供注册表数据引用）。")
    lines.append(" * @note 由 scripts/gen_tag_names.py 从客户端 jar 生成。")
    lines.append(" */")
    lines.append("#ifndef _MC_TAG_NAMES_H_")
    lines.append("#define _MC_TAG_NAMES_H_")
    lines.append("")
    lines.append("#include <stdint.h>")
    lines.append("")
    idx = 0
    reg_keys = sorted(groups.keys())
    for reg in reg_keys:
        tags = sorted(set(groups[reg]))
        lines.append("static const char *const mc_tag_%d[] = {" % idx)
        for t in tags:
            lines.append('  "minecraft:%s",' % t)
        lines.append("};")
        idx += 1
    lines.append("")
    lines.append("typedef struct { const char *registry; uint16_t count;"
                 " const char *const *tags; } mc_tag_group_t;")
    lines.append("")
    lines.append("static const mc_tag_group_t mc_tag_groups[] = {")
    for i, reg in enumerate(reg_keys):
        lines.append('  {"minecraft:%s", %dU, mc_tag_%d},' %
                     (reg, len(set(groups[reg])), i))
    lines.append("};")
    lines.append("")
    lines.append("#define MC_TAG_GROUP_COUNT %dU" % len(reg_keys))
    lines.append("")
    lines.append("#endif /* _MC_TAG_NAMES_H_ */")

    with open(out_path, "w") as f:
        f.write("\n".join(lines) + "\n")
    total = sum(len(set(v)) for v in groups.values())
    print("wrote", out_path, "groups:", len(reg_keys), "tags:", total)


if __name__ == "__main__":
    main()
