#!/usr/bin/env python3
"""从 Minecraft 客户端 jar 提取同步注册表的条目名，生成 C 头文件。

用法: python3 scripts/gen_registry_names.py <client.jar> [输出路径]
默认输出 project/code/mc_registry_names.h
条目名只取名字（数据由客户端 known pack 提供）。
"""

import os
import sys
import zipfile

# (packet registry id -> jar data 子目录)；对部分协议引用要求特定顺序。
REGISTRIES = [
    ("worldgen/biome", "worldgen/biome", ["plains"]),
    ("dimension_type", "dimension_type", ["overworld"]),
    ("chat_type", "chat_type", []),
    ("trim_pattern", "trim_pattern", []),
    ("trim_material", "trim_material", []),
    ("wolf_variant", "wolf_variant", []),
    ("wolf_sound_variant", "wolf_sound_variant", []),
    ("pig_variant", "pig_variant", []),
    ("pig_sound_variant", "pig_sound_variant", []),
    ("frog_variant", "frog_variant", []),
    ("cat_variant", "cat_variant", []),
    ("cat_sound_variant", "cat_sound_variant", []),
    ("cow_variant", "cow_variant", []),
    ("cow_sound_variant", "cow_sound_variant", []),
    ("chicken_variant", "chicken_variant", []),
    ("chicken_sound_variant", "chicken_sound_variant", []),
    ("zombie_nautilus_variant", "zombie_nautilus_variant", []),
    ("painting_variant", "painting_variant", []),
    ("sulfur_cube_archetype", "sulfur_cube_archetype", []),
    ("damage_type", "damage_type", []),
    ("banner_pattern", "banner_pattern", []),
    ("enchantment", "enchantment", []),
    ("jukebox_song", "jukebox_song", []),
    ("instrument", "instrument", []),
    ("test_environment", "test_environment", []),
    ("test_instance", "test_instance", []),
    ("dialog", "dialog", []),
    ("world_clock", "world_clock", []),
    ("timeline", "timeline", []),
    ("decorated_pot_pattern", "decorated_pot_pattern", []),
    ("block_transformer", "block_transformer", []),
    ("block_state_provider", "block_state_provider", []),
]


def list_dir(zf, names_set, subdir):
    prefix = "data/minecraft/%s/" % subdir
    out = []
    for n in names_set:
        if n.startswith(prefix) and n.endswith(".json"):
            rel = n[len(prefix):-len(".json")]
            if "/" in rel:
                continue
            out.append(rel)
    return sorted(out)


def main():
    jar = sys.argv[1]
    out_path = sys.argv[2] if len(sys.argv) > 2 else os.path.join(
        "project", "code", "mc_registry_names.h")
    zf = zipfile.ZipFile(jar)
    names_set = set(zf.namelist())

    regs = []
    for reg_id, subdir, first in REGISTRIES:
        entries = list_dir(zf, names_set, subdir)
        for f in reversed(first):
            if f in entries:
                entries.remove(f)
                entries.insert(0, f)
        regs.append((reg_id, entries))

    lines = []
    lines.append("/**")
    lines.append(" * @file mc_registry_names.h")
    lines.append(" * @brief 同步注册表的条目名列表（数据由客户端 known pack 提供）。")
    lines.append(" * @note 由 scripts/gen_registry_names.py 从客户端 jar 生成。")
    lines.append(" */")
    lines.append("#ifndef _MC_REGISTRY_NAMES_H_")
    lines.append("#define _MC_REGISTRY_NAMES_H_")
    lines.append("")
    lines.append("#include <stdint.h>")
    lines.append("")
    for i, (reg_id, entries) in enumerate(regs):
        lines.append("static const char *const mc_reg_%d[] = {" % i)
        for e in entries:
            lines.append('  "minecraft:%s",' % e)
        if not entries:
            lines.append("  (const char *)0,")
        lines.append("};")
    lines.append("")
    lines.append("typedef struct {")
    lines.append("  const char *registry;")
    lines.append("  uint16_t count;")
    lines.append("  const char *const *names;")
    lines.append("} mc_registry_desc_t;")
    lines.append("")
    lines.append("static const mc_registry_desc_t mc_registries[] = {")
    for i, (reg_id, entries) in enumerate(regs):
        lines.append('  {"minecraft:%s", %dU, mc_reg_%d},' %
                     (reg_id, len(entries), i))
    lines.append("};")
    lines.append("")
    lines.append("#define MC_REGISTRY_COUNT %dU" % len(regs))
    lines.append("")
    lines.append("#endif /* _MC_REGISTRY_NAMES_H_ */")

    with open(out_path, "w") as f:
        f.write("\n".join(lines) + "\n")
    total = sum(len(e) for _, e in regs)
    print("wrote", out_path, "registries:", len(regs), "entries:", total)
    for reg_id, entries in regs:
        print("  %-28s %d" % (reg_id, len(entries)))


if __name__ == "__main__":
    main()
