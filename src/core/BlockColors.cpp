#include "core/BlockColors.h"

#include <array>
#include <string>
#include <unordered_map>

namespace justnbt {
namespace {
enum : uint32_t {
    GRASS = 0x7FB238,
    SAND = 0xF7E9A3,
    WOOL = 0xC7C7C7,
    FIRE = 0xFF0000,
    ICE = 0xA0A0FF,
    METAL = 0xA7A7A7,
    PLANT = 0x007C00,
    SNOW = 0xFFFFFF,
    CLAY = 0xA4A8B8,
    DIRT = 0x976D4D,
    STONE = 0x707070,
    WATER = 0x4040FF,
    WOOD = 0x8F7748,
    QUARTZ = 0xFFFCF5,
    ORANGE = 0xD87F33,
    MAGENTA = 0xB24CD8,
    LIGHT_BLUE = 0x6699D8,
    YELLOW = 0xE5E533,
    LIME = 0x7FCC19,
    PINK = 0xF27FA5,
    GRAY = 0x4C4C4C,
    LIGHT_GRAY = 0x999999,
    CYAN = 0x4C7F99,
    PURPLE = 0x7F3FB2,
    BLUE = 0x334CB2,
    BROWN = 0x664C33,
    GREEN = 0x667F33,
    RED = 0x993333,
    BLACK = 0x191919,
    GOLD = 0xFAEE4D,
    DIAMOND = 0x5CDBD5,
    LAPIS = 0x4A80FF,
    EMERALD = 0x00D93A,
    PODZOL = 0x815631,
    NETHER = 0x700200,
    T_WHITE = 0xD1B1A1,
    T_ORANGE = 0x9F5224,
    T_MAGENTA = 0x95576C,
    T_LIGHT_BLUE = 0x706C8A,
    T_YELLOW = 0xBA8524,
    T_LIME = 0x677535,
    T_PINK = 0xA04D4E,
    T_GRAY = 0x392923,
    T_LIGHT_GRAY = 0x876B62,
    T_CYAN = 0x575C5C,
    T_PURPLE = 0x7A4958,
    T_BLUE = 0x4C3E5C,
    T_BROWN = 0x4C3223,
    T_GREEN = 0x4C522A,
    T_RED = 0x8E3C2E,
    T_BLACK = 0x251610,
    CRIMSON_NYLIUM = 0xBD3031,
    CRIMSON_STEM = 0x943F61,
    CRIMSON_HYPHAE = 0x5C191D,
    WARPED_NYLIUM = 0x167E86,
    WARPED_STEM = 0x3A8E8C,
    WARPED_HYPHAE = 0x562C3E,
    WARPED_WART = 0x14B485,
    DEEPSLATE = 0x646464,
    RAW_IRON = 0xD8AF93,
    GLOW_LICHEN = 0x7FA796,
};

struct Dye {
    std::string_view name;
    uint32_t color;
    uint32_t terracotta;
};
constexpr std::array<Dye, 16> kDyes{{
    {"white", SNOW, T_WHITE},          {"orange", ORANGE, T_ORANGE},  {"magenta", MAGENTA, T_MAGENTA},
    {"light_blue", LIGHT_BLUE, T_LIGHT_BLUE}, {"yellow", YELLOW, T_YELLOW}, {"lime", LIME, T_LIME},
    {"pink", PINK, T_PINK},             {"gray", GRAY, T_GRAY},        {"light_gray", LIGHT_GRAY, T_LIGHT_GRAY},
    {"cyan", CYAN, T_CYAN},             {"purple", PURPLE, T_PURPLE},  {"blue", BLUE, T_BLUE},
    {"brown", BROWN, T_BROWN},          {"green", GREEN, T_GREEN},     {"red", RED, T_RED},
    {"black", BLACK, T_BLACK},
}};

struct Wood {
    std::string_view name;
    uint32_t planks;
};
constexpr std::array<Wood, 12> kWoods{{
    {"oak", WOOD},          {"spruce", PODZOL},        {"birch", SAND},       {"jungle", DIRT},
    {"acacia", ORANGE},     {"dark_oak", BROWN},       {"mangrove", RED},     {"cherry", T_WHITE},
    {"bamboo", YELLOW},     {"crimson", CRIMSON_STEM}, {"warped", WARPED_STEM}, {"pale_oak", QUARTZ},
}};

bool startsWith(std::string_view s, std::string_view p) { return s.substr(0, p.size()) == p; }
bool endsWith(std::string_view s, std::string_view p)
{
    return s.size() >= p.size() && s.substr(s.size() - p.size()) == p;
}
bool contains(std::string_view s, std::string_view p) { return s.find(p) != std::string_view::npos; }

const std::unordered_map<std::string_view, uint32_t>& exactColors()
{
    static const std::unordered_map<std::string_view, uint32_t> map{
        {"grass_block", GRASS}, {"dirt", DIRT}, {"coarse_dirt", DIRT}, {"rooted_dirt", DIRT}, {"farmland", DIRT},
        {"dirt_path", DIRT}, {"mud", CYAN}, {"podzol", PODZOL}, {"mycelium", PURPLE}, {"sand", SAND},
        {"suspicious_sand", SAND}, {"red_sand", ORANGE}, {"gravel", STONE}, {"suspicious_gravel", STONE},
        {"stone", STONE}, {"cobblestone", STONE}, {"mossy_cobblestone", STONE}, {"smooth_stone", STONE},
        {"andesite", STONE}, {"polished_andesite", STONE}, {"diorite", QUARTZ}, {"polished_diorite", QUARTZ},
        {"granite", DIRT}, {"polished_granite", DIRT}, {"tuff", T_GRAY}, {"calcite", T_WHITE},
        {"dripstone_block", T_BROWN}, {"pointed_dripstone", T_BROWN}, {"bedrock", STONE}, {"clay", CLAY},
        {"lava", FIRE}, {"fire", FIRE}, {"magma_block", NETHER}, {"ice", ICE}, {"packed_ice", ICE},
        {"blue_ice", ICE}, {"frosted_ice", ICE}, {"snow", SNOW}, {"snow_block", SNOW}, {"powder_snow", SNOW},
        {"netherrack", NETHER}, {"nether_wart_block", RED}, {"warped_wart_block", WARPED_WART},
        {"crimson_nylium", CRIMSON_NYLIUM}, {"warped_nylium", WARPED_NYLIUM}, {"soul_sand", BROWN},
        {"soul_soil", BROWN}, {"basalt", BLACK}, {"polished_basalt", BLACK}, {"smooth_basalt", BLACK},
        {"blackstone", BLACK}, {"glowstone", SAND}, {"shroomlight", RED}, {"end_stone", SAND},
        {"end_stone_bricks", SAND}, {"purpur_block", MAGENTA}, {"purpur_pillar", MAGENTA}, {"obsidian", BLACK},
        {"crying_obsidian", BLACK}, {"ancient_debris", BLACK}, {"iron_block", METAL}, {"gold_block", GOLD},
        {"diamond_block", DIAMOND}, {"emerald_block", EMERALD}, {"lapis_block", LAPIS}, {"redstone_block", FIRE},
        {"coal_block", BLACK}, {"netherite_block", BLACK}, {"copper_block", ORANGE}, {"raw_iron_block", RAW_IRON},
        {"raw_gold_block", GOLD}, {"raw_copper_block", ORANGE}, {"amethyst_block", PURPLE},
        {"budding_amethyst", PURPLE}, {"moss_block", GREEN}, {"moss_carpet", GREEN}, {"pale_moss_block", LIGHT_GRAY},
        {"pale_moss_carpet", LIGHT_GRAY}, {"sculk", BLACK}, {"sculk_catalyst", BLACK}, {"sculk_shrieker", BLACK},
        {"sculk_sensor", CYAN}, {"glow_lichen", GLOW_LICHEN}, {"hay_block", YELLOW}, {"melon", LIME},
        {"pumpkin", ORANGE}, {"carved_pumpkin", ORANGE}, {"jack_o_lantern", ORANGE}, {"cactus", PLANT},
        {"bookshelf", WOOD}, {"crafting_table", WOOD}, {"chest", WOOD}, {"trapped_chest", WOOD}, {"barrel", WOOD},
        {"note_block", WOOD}, {"jukebox", DIRT}, {"furnace", STONE}, {"blast_furnace", STONE}, {"smoker", WOOD},
        {"tnt", FIRE}, {"sponge", YELLOW}, {"wet_sponge", YELLOW}, {"slime_block", GRASS}, {"honey_block", ORANGE},
        {"honeycomb_block", ORANGE}, {"bee_nest", YELLOW}, {"beehive", WOOD}, {"prismarine", CYAN},
        {"prismarine_bricks", DIAMOND}, {"dark_prismarine", DIAMOND}, {"sea_lantern", QUARTZ},
        {"dried_kelp_block", GREEN}, {"brick_block", RED}, {"bricks", RED}, {"mud_bricks", T_LIGHT_GRAY},
        {"packed_mud", DIRT}, {"terracotta", ORANGE}, {"quartz_block", QUARTZ}, {"smooth_quartz", QUARTZ},
        {"quartz_bricks", QUARTZ}, {"quartz_pillar", QUARTZ}, {"chiseled_quartz_block", QUARTZ},
        {"nether_bricks", NETHER}, {"red_nether_bricks", NETHER}, {"stone_bricks", STONE},
        {"mossy_stone_bricks", STONE}, {"cracked_stone_bricks", STONE}, {"chiseled_stone_bricks", STONE},
        {"brown_mushroom_block", DIRT}, {"red_mushroom_block", RED}, {"mushroom_stem", WOOL},
        {"sandstone", SAND}, {"red_sandstone", ORANGE}, {"snow_layer", SNOW}, {"cobweb", WOOL},
        {"target", QUARTZ}, {"lectern", WOOD}, {"composter", WOOD}, {"cauldron", STONE}, {"anvil", METAL},
        {"hopper", STONE}, {"observer", STONE}, {"dispenser", STONE}, {"dropper", STONE}, {"piston", STONE},
        {"sticky_piston", STONE}, {"spawner", STONE}, {"beacon", DIAMOND}, {"enchanting_table", RED},
        {"end_portal_frame", GREEN}, {"respawn_anchor", BLACK}, {"lodestone", METAL}, {"bell", GOLD},
        {"lantern", METAL}, {"soul_lantern", METAL}, {"chain", METAL}, {"iron_bars", METAL}, {"ladder", WOOD},
        {"scaffolding", SAND}, {"lily_pad", PLANT}, {"sugar_cane", PLANT}, {"bamboo", PLANT}, {"vine", PLANT},
        {"big_dripleaf", PLANT}, {"small_dripleaf", PLANT}, {"azalea", PLANT}, {"flowering_azalea", PLANT},
        {"sweet_berry_bush", PLANT}, {"cave_vines", PLANT}, {"cave_vines_plant", PLANT},
        {"nether_portal", PURPLE}, {"end_portal", BLACK}, {"end_gateway", BLACK}, {"dragon_egg", BLACK},
        {"chorus_plant", PURPLE}, {"chorus_flower", PURPLE}, {"crimson_roots", NETHER}, {"warped_roots", CYAN},
        {"nether_sprouts", CYAN}, {"weeping_vines", NETHER}, {"twisting_vines", CYAN}, {"nether_wart", RED},
        {"trial_spawner", STONE}, {"vault", STONE}, {"heavy_core", METAL}, {"crafter", STONE},
        {"resin_block", T_ORANGE}, {"creaking_heart", ORANGE}, {"amethyst_cluster", PURPLE},
        {"large_amethyst_bud", PURPLE}, {"medium_amethyst_bud", PURPLE}, {"small_amethyst_bud", PURPLE},
        {"campfire", PODZOL}, {"soul_campfire", PODZOL}, {"soul_fire", LIGHT_BLUE}, {"cocoa", PLANT},
        {"iron_chain", METAL}, {"iron_door", METAL}, {"iron_trapdoor", METAL}, {"loom", WOOD},
        {"smithing_table", WOOD}, {"fletching_table", SAND}, {"cartography_table", WOOD}, {"sculk_vein", BLACK},
        {"spore_blossom", PLANT}, {"bone_block", SAND}, {"decorated_pot", T_RED}, {"water_cauldron", STONE},
        {"lava_cauldron", STONE}, {"powder_snow_cauldron", STONE}, {"grindstone", METAL}, {"stonecutter", STONE},
        {"brewing_stand", METAL}, {"ender_chest", BLACK}, {"conduit", DIAMOND}, {"daylight_detector", WOOD},
        {"lightning_rod", ORANGE}, {"hanging_roots", DIRT}, {"frogspawn", WATER}, {"sniffer_egg", RED},
        {"turtle_egg", SAND}, {"sea_pickle", GREEN}, {"mangrove_roots", PODZOL}, {"muddy_mangrove_roots", PODZOL},
    };
    return map;
}

bool isSeeThrough(std::string_view n)
{
    static const std::unordered_map<std::string_view, bool> exact{
        {"air", true}, {"cave_air", true}, {"void_air", true}, {"light", true}, {"barrier", true},
        {"structure_void", true}, {"glass", true}, {"glass_pane", true}, {"tinted_glass", true},
        {"torch", true}, {"wall_torch", true}, {"soul_torch", true}, {"soul_wall_torch", true},
        {"redstone_torch", true}, {"redstone_wall_torch", true}, {"redstone_wire", true}, {"lever", true},
        {"tripwire", true}, {"tripwire_hook", true}, {"repeater", true}, {"comparator", true},
        {"flower_pot", true}, {"end_rod", true}, {"moving_piston", true}, {"string", true}, {"copper_torch", true},
        {"copper_wall_torch", true},
    };
    if (exact.count(n))
        return true;
    return endsWith(n, "rail") || endsWith(n, "_button") || startsWith(n, "potted_")
        || (endsWith(n, "_glass") && !contains(n, "stained")) || (endsWith(n, "_glass_pane") && !contains(n, "stained"));
}

bool isAlwaysWater(std::string_view n)
{
    return n == "water" || n == "bubble_column" || n == "kelp" || n == "kelp_plant" || n == "seagrass"
        || n == "tall_seagrass";
}

BlockStyle classify(std::string_view n)
{
    BlockStyle s;
    if (isSeeThrough(n)) {
        s.transparent = true;
        return s;
    }
    if (isAlwaysWater(n)) {
        s.water = true;
        s.rgb = WATER;
        return s;
    }
    if (auto it = exactColors().find(n); it != exactColors().end()) {
        s.rgb = it->second;
        return s;
    }

    static constexpr std::string_view kDyed[] = {
        "wool", "carpet", "concrete", "concrete_powder", "terracotta", "glazed_terracotta", "stained_glass",
        "stained_glass_pane", "bed", "shulker_box",
    };
    for (const Dye& d : kDyes) {
        if (!startsWith(n, d.name) || n.size() <= d.name.size() || n[d.name.size()] != '_')
            continue;
        const std::string_view rest = n.substr(d.name.size() + 1);
        for (std::string_view suffix : kDyed) {
            if (rest == suffix) {
                s.rgb = rest == "terracotta" ? d.terracotta : d.color;
                return s;
            }
        }
    }

    if (contains(n, "deepslate")) {
        s.rgb = DEEPSLATE;
        return s;
    }
    if (endsWith(n, "_ore")) {
        s.rgb = contains(n, "nether") ? NETHER : STONE;
        return s;
    }
    if (endsWith(n, "leaves")) {
        s.rgb = contains(n, "cherry") ? PINK : PLANT;
        return s;
    }
    if (contains(n, "copper")) {
        s.rgb = contains(n, "oxidized") ? WARPED_NYLIUM : contains(n, "weathered") ? WARPED_STEM
            : contains(n, "exposed") ? T_LIGHT_GRAY : ORANGE;
        return s;
    }
    if (contains(n, "crimson")) {
        s.rgb = contains(n, "hyphae") ? CRIMSON_HYPHAE : CRIMSON_STEM;
        return s;
    }
    if (contains(n, "warped")) {
        s.rgb = contains(n, "hyphae") ? WARPED_HYPHAE : WARPED_STEM;
        return s;
    }
    for (const Wood& w : kWoods) {
        if (startsWith(n, w.name) || startsWith(n, std::string("stripped_") + std::string(w.name))) {
            s.rgb = w.planks;
            return s;
        }
    }
    if (contains(n, "sandstone")) {
        s.rgb = startsWith(n, "red_") ? ORANGE : SAND;
        return s;
    }
    if (contains(n, "blackstone") || contains(n, "basalt")) {
        s.rgb = BLACK;
        return s;
    }
    if (contains(n, "nether_brick")) {
        s.rgb = NETHER;
        return s;
    }
    if (contains(n, "quartz")) {
        s.rgb = QUARTZ;
        return s;
    }
    if (contains(n, "prismarine")) {
        s.rgb = CYAN;
        return s;
    }
    if (contains(n, "purpur")) {
        s.rgb = MAGENTA;
        return s;
    }
    if (contains(n, "end_stone")) {
        s.rgb = SAND;
        return s;
    }
    if (contains(n, "tuff")) {
        s.rgb = T_GRAY;
        return s;
    }
    if (contains(n, "mud_brick")) {
        s.rgb = T_LIGHT_GRAY;
        return s;
    }
    if (contains(n, "brick")) {
        s.rgb = RED;
        return s;
    }
    if (contains(n, "stone") || contains(n, "cobble") || contains(n, "andesite")) {
        s.rgb = STONE;
        return s;
    }
    if (contains(n, "granite")) {
        s.rgb = DIRT;
        return s;
    }
    if (contains(n, "diorite")) {
        s.rgb = QUARTZ;
        return s;
    }
    if (contains(n, "coral")) {
        s.rgb = contains(n, "dead") ? GRAY
            : contains(n, "tube") ? BLUE : contains(n, "brain") ? PINK : contains(n, "bubble") ? PURPLE
            : contains(n, "fire") ? RED : YELLOW;
        return s;
    }
    static constexpr std::string_view kPlants[] = {
        "grass", "fern", "flower", "tulip", "orchid", "allium", "bluet", "daisy", "poppy", "dandelion", "rose",
        "lilac", "peony", "cornflower", "lily_of_the_valley", "sunflower", "sapling", "wheat", "carrots", "potatoes",
        "beetroots", "stem", "bush", "petals", "roots", "propagule", "pitcher", "torchflower", "mushroom", "hanging_moss",
        "eyeblossom", "leaf_litter", "wildflowers", "cactus_flower", "firefly_bush",
    };
    for (std::string_view p : kPlants) {
        if (contains(n, p)) {
            s.rgb = PLANT;
            return s;
        }
    }
    if (contains(n, "candle") || endsWith(n, "_head") || endsWith(n, "_skull") || contains(n, "sign")
        || contains(n, "banner") || contains(n, "pressure_plate") || contains(n, "head")) {
        s.transparent = true;
        return s;
    }
    if (contains(n, "shulker_box")) {
        s.rgb = PURPLE;
        return s;
    }
    s.known = false;
    return s;
}
}

BlockStyle blockStyle(std::string_view name, bool waterlogged)
{
    if (startsWith(name, "minecraft:"))
        name.remove_prefix(10);
    BlockStyle s = classify(name);
    if (waterlogged && !s.water) {
        s.water = true;
        s.transparent = false;
        s.rgb = WATER;
    }
    return s;
}
}
