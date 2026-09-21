/**
 * @file mc_tag_names.h
 * @brief 原版标签名列表（以空标签下发，供注册表数据引用）。
 * @note 由 scripts/gen_tag_names.py 从客户端 jar 生成。
 */
#ifndef _MC_TAG_NAMES_H_
#define _MC_TAG_NAMES_H_

#include <stdint.h>

static const char *const mc_tag_0[] = {
  "minecraft:no_item_required",
  "minecraft:pattern_item/bordure_indented",
  "minecraft:pattern_item/creeper",
  "minecraft:pattern_item/field_masoned",
  "minecraft:pattern_item/flow",
  "minecraft:pattern_item/flower",
  "minecraft:pattern_item/globe",
  "minecraft:pattern_item/guster",
  "minecraft:pattern_item/mojang",
  "minecraft:pattern_item/piglin",
  "minecraft:pattern_item/skull",
};
static const char *const mc_tag_1[] = {
  "minecraft:always_hurts_ender_dragons",
  "minecraft:always_kills_armor_stands",
  "minecraft:always_most_significant_fall",
  "minecraft:always_triggers_silverfish",
  "minecraft:avoids_guardian_thorns",
  "minecraft:burn_from_stepping",
  "minecraft:burns_armor_stands",
  "minecraft:bypasses_armor",
  "minecraft:bypasses_cooldown",
  "minecraft:bypasses_effects",
  "minecraft:bypasses_enchantments",
  "minecraft:bypasses_invulnerability",
  "minecraft:bypasses_resistance",
  "minecraft:bypasses_shield",
  "minecraft:bypasses_wolf_armor",
  "minecraft:can_break_armor_stand",
  "minecraft:damages_helmet",
  "minecraft:ignites_armor_stands",
  "minecraft:is_drowning",
  "minecraft:is_explosion",
  "minecraft:is_fall",
  "minecraft:is_fire",
  "minecraft:is_freezing",
  "minecraft:is_lightning",
  "minecraft:is_player_attack",
  "minecraft:is_projectile",
  "minecraft:mace_smash",
  "minecraft:no_anger",
  "minecraft:no_impact",
  "minecraft:no_knockback",
  "minecraft:no_wolf_retaliation",
  "minecraft:panic_causes",
  "minecraft:panic_environmental_causes",
  "minecraft:sulfur_cube_with_block_immune_to",
  "minecraft:witch_resistant_to",
  "minecraft:wither_immune_to",
};
static const char *const mc_tag_2[] = {
  "minecraft:pause_screen_additions",
  "minecraft:quick_actions",
};
static const char *const mc_tag_3[] = {
  "minecraft:curse",
  "minecraft:double_trade_price",
  "minecraft:exclusive_set/armor",
  "minecraft:exclusive_set/boots",
  "minecraft:exclusive_set/bow",
  "minecraft:exclusive_set/crossbow",
  "minecraft:exclusive_set/damage",
  "minecraft:exclusive_set/mining",
  "minecraft:exclusive_set/riptide",
  "minecraft:in_enchanting_table",
  "minecraft:non_treasure",
  "minecraft:on_mob_spawn_equipment",
  "minecraft:on_random_loot",
  "minecraft:on_traded_equipment",
  "minecraft:prevents_bee_spawns_when_mining",
  "minecraft:prevents_decorated_pot_shattering",
  "minecraft:prevents_ice_melting",
  "minecraft:prevents_infested_spawns",
  "minecraft:smelts_loot",
  "minecraft:tooltip_order",
  "minecraft:tradeable",
  "minecraft:treasure",
};
static const char *const mc_tag_4[] = {
  "minecraft:goat_horns",
  "minecraft:regular_goat_horns",
  "minecraft:screaming_goat_horns",
};
static const char *const mc_tag_5[] = {
  "minecraft:placeable",
};
static const char *const mc_tag_6[] = {
  "minecraft:in_end",
  "minecraft:in_nether",
  "minecraft:in_overworld",
  "minecraft:universal",
};

typedef struct { const char *registry; uint16_t count; const char *const *tags; } mc_tag_group_t;

static const mc_tag_group_t mc_tag_groups[] = {
  {"minecraft:banner_pattern", 11U, mc_tag_0},
  {"minecraft:damage_type", 36U, mc_tag_1},
  {"minecraft:dialog", 2U, mc_tag_2},
  {"minecraft:enchantment", 22U, mc_tag_3},
  {"minecraft:instrument", 3U, mc_tag_4},
  {"minecraft:painting_variant", 1U, mc_tag_5},
  {"minecraft:timeline", 4U, mc_tag_6},
};

#define MC_TAG_GROUP_COUNT 7U

#endif /* _MC_TAG_NAMES_H_ */
