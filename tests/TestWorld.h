#pragma once

#include "core/NbtFile.h"
#include "core/NbtIO.h"
#include "core/Region.h"

#include <QDir>
#include <QFile>

#include <map>
#include <tuple>
#include <vector>

namespace testworld {
using nbt::Tag;
using nbt::TagType;

inline Tag* add(Tag* parent, TagType type, const char* name = "")
{
    return parent->append(std::make_unique<Tag>(type, name));
}
inline Tag* addString(Tag* parent, const char* name, const char* value)
{
    Tag* t = add(parent, TagType::String, name);
    t->string = value;
    return t;
}
inline Tag* addInt(Tag* parent, const char* name, int64_t value, TagType type = TagType::Int)
{
    Tag* t = add(parent, type, name);
    t->integer = value;
    return t;
}
inline Tag* addList(Tag* parent, const char* name, TagType of)
{
    Tag* t = add(parent, TagType::List, name);
    t->listType = of;
    return t;
}
inline void addPos(Tag* entity, double x, double y, double z)
{
    Tag* pos = addList(entity, "Pos", TagType::Double);
    for (double v : {x, y, z})
        add(pos, TagType::Double)->floating = v;
}
inline Tag* addItem(Tag* list, const char* id, int count, int slot = -1)
{
    Tag* item = add(list, TagType::Compound);
    if (slot >= 0)
        addInt(item, "Slot", slot, TagType::Byte);
    addString(item, "id", id);
    addInt(item, "count", count);
    return item;
}

inline std::unique_ptr<Tag> chunk(int cx, int cz, const std::vector<const char*>& palette,
                           const std::map<int, std::map<int, int>>& blocks)
{
    auto root = std::make_unique<Tag>(TagType::Compound);
    addInt(root.get(), "DataVersion", 3953);
    addInt(root.get(), "xPos", cx);
    addInt(root.get(), "zPos", cz);
    addString(root.get(), "Status", "minecraft:full");
    Tag* sections = addList(root.get(), "sections", TagType::Compound);
    for (const auto& [y, placed] : blocks) {
        Tag* s = add(sections, TagType::Compound);
        addInt(s, "Y", y, TagType::Byte);
        Tag* states = add(s, TagType::Compound, "block_states");
        Tag* pal = addList(states, "palette", TagType::Compound);
        for (const char* name : palette)
            addString(add(pal, TagType::Compound), "Name", name);
        Tag* data = add(states, TagType::LongArray, "data");
        data->longs.assign(256, 0);
        for (const auto& [index, entry] : placed)
            data->longs[size_t(index / 16)] |= int64_t(uint64_t(entry) << ((index % 16) * 4));
    }
    return root;
}

inline int at(int x, int y, int z)
{
    return ((y & 15) * 16 + (z & 15)) * 16 + (x & 15);
}

inline void writeChunk(const QString& folder, int cx, int cz, const Tag& root)
{
    QDir().mkpath(folder);
    std::vector<uint8_t> bytes;
    nbt::write(root, nbt::Endian::Big, bytes);
    const int rx = cx >> 5, rz = cz >> 5;
    justnbt::RegionFile::writeChunk(folder + QStringLiteral("/r.%1.%2.mca").arg(rx).arg(rz), cx - rx * 32, cz - rz * 32, bytes, 2);
}

inline bool writeLevelDat(const QString& path)
{
    justnbt::NbtFile file;
    file.format.endian = nbt::Endian::Big;
    file.format.compression = justnbt::Compression::Gzip;
    file.root = std::make_unique<Tag>(TagType::Compound);
    Tag* data = add(file.root.get(), TagType::Compound, "Data");
    addString(data, "LevelName", "Search test");
    addInt(data, "DataVersion", 3953);
    const auto bytes = file.encode();
    QFile out(path);
    return out.open(QIODevice::WriteOnly)
        && out.write(reinterpret_cast<const char*>(bytes.data()), qint64(bytes.size())) == qint64(bytes.size());
}

inline bool buildWorld(const QString& dir)
{
    QDir().mkpath(dir);
    if (!writeLevelDat(dir + QStringLiteral("/level.dat")))
        return false;
    const std::vector<const char*> palette{"minecraft:stone", "minecraft:diamond_ore",
                                           "minecraft:deepslate_diamond_ore", "minecraft:chest", "minecraft:spawner"};
    auto c00 = chunk(0, 0, palette,
                     {{0, {{at(2, 4, 3), 1}, {at(10, 1, 12), 2}, {at(5, 3, 7), 3}, {at(1, 2, 1), 4}}}});
    Tag* bes = addList(c00.get(), "block_entities", TagType::Compound);
    {
        Tag* chest = add(bes, TagType::Compound);
        addString(chest, "id", "minecraft:chest");
        addInt(chest, "x", 5);
        addInt(chest, "y", 3);
        addInt(chest, "z", 7);
        Tag* name = add(chest, TagType::Compound, "CustomName");
        addString(name, "text", "Treasure");
        Tag* items = addList(chest, "Items", TagType::Compound);
        Tag* shulker = addItem(items, "minecraft:shulker_box", 1, 0);
        Tag* container = addList(add(shulker, TagType::Compound, "components"), "minecraft:container", TagType::Compound);
        Tag* slot = add(container, TagType::Compound);
        addInt(slot, "slot", 0);
        Tag* inner = add(slot, TagType::Compound, "item");
        addString(inner, "id", "minecraft:diamond");
        addInt(inner, "count", 5);
        addItem(items, "minecraft:diamond", 3, 1);

        Tag* spawner = add(bes, TagType::Compound);
        addString(spawner, "id", "minecraft:mob_spawner");
        addInt(spawner, "x", 1);
        addInt(spawner, "y", 2);
        addInt(spawner, "z", 1);
        addString(add(add(spawner, TagType::Compound, "SpawnData"), TagType::Compound, "entity"), "id", "minecraft:zombie");
    }
    writeChunk(dir + QStringLiteral("/region"), 0, 0, *c00);
    writeChunk(dir + QStringLiteral("/region"), 3, 1, *chunk(3, 1, palette, {{-1, {{at(4, -3, 4), 1}}}}));
    writeChunk(dir + QStringLiteral("/region"), -5, 2, *chunk(-5, 2, palette, {{2, {{at(0, 40, 0), 1}}}}));

    auto ents = std::make_unique<Tag>(TagType::Compound);
    addInt(ents.get(), "DataVersion", 3953);
    Tag* list = addList(ents.get(), "Entities", TagType::Compound);
    {
        Tag* villager = add(list, TagType::Compound);
        addString(villager, "id", "minecraft:villager");
        addPos(villager, 4.5, 64.0, 4.5);
        addString(villager, "CustomName", R"({"text":"Bob"})");
        Tag* data = add(villager, TagType::Compound, "VillagerData");
        addString(data, "profession", "minecraft:librarian");
        Tag* recipes = addList(add(villager, TagType::Compound, "Offers"), "Recipes", TagType::Compound);
        Tag* recipe = add(recipes, TagType::Compound);
        Tag* buy = add(recipe, TagType::Compound, "buy");
        addString(buy, "id", "minecraft:diamond");
        addInt(buy, "count", 10);

        Tag* zombie = add(list, TagType::Compound);
        addString(zombie, "id", "minecraft:zombie");
        addPos(zombie, -2.2, 70.9, 9.7);
        Tag* hands = addList(zombie, "HandItems", TagType::Compound);
        addItem(hands, "minecraft:diamond_sword", 1);
        add(hands, TagType::Compound);
        Tag* passengers = addList(zombie, "Passengers", TagType::Compound);
        Tag* chicken = add(passengers, TagType::Compound);
        addString(chicken, "id", "minecraft:chicken");
        addPos(chicken, -2.2, 72.0, 9.7);

        Tag* dropped = add(list, TagType::Compound);
        addString(dropped, "id", "minecraft:item");
        addPos(dropped, 8.1, 63.0, 8.9);
        Tag* item = add(dropped, TagType::Compound, "Item");
        addString(item, "id", "minecraft:diamond");
        addInt(item, "count", 2);
    }
    writeChunk(dir + QStringLiteral("/entities"), 0, 0, *ents);

    auto poi = std::make_unique<Tag>(TagType::Compound);
    addInt(poi.get(), "DataVersion", 3953);
    Tag* section = add(add(poi.get(), TagType::Compound, "Sections"), TagType::Compound, "4");
    Tag* records = addList(section, "Records", TagType::Compound);
    for (const auto& [type, x, y, z, free] : {std::tuple{"minecraft:home", 3, 70, 3, 1},
                                             std::tuple{"minecraft:librarian", 6, 65, 6, 0}}) {
        Tag* r = add(records, TagType::Compound);
        addString(r, "type", type);
        Tag* pos = add(r, TagType::IntArray, "pos");
        pos->ints = {x, y, z};
        addInt(r, "free_tickets", free);
    }
    writeChunk(dir + QStringLiteral("/poi"), 0, 0, *poi);
    return true;
}
}
