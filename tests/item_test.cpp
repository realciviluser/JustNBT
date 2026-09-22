#include "core/ItemInfo.h"
#include "core/TagInfo.h"

#include <QCoreApplication>

#include <cstdio>

using justnbt::ItemStack;
using nbt::Tag;
using nbt::TagType;

namespace {
int failures = 0;

void check(bool ok, const QString& what)
{
    std::printf("%-62s %s\n", qPrintable(what), ok ? "ok" : "FAILED");
    if (!ok)
        ++failures;
}

Tag* add(Tag* parent, TagType type, const char* name)
{
    return parent->append(std::make_unique<Tag>(type, name));
}

void addEnchant(Tag* list, const char* id, int level)
{
    Tag* one = list->append(std::make_unique<Tag>(TagType::Compound));
    add(one, TagType::String, "id")->string = id;
    add(one, TagType::Short, "lvl")->integer = level;
}

void addEnchantNumeric(Tag* list, int id, int level)
{
    Tag* one = list->append(std::make_unique<Tag>(TagType::Compound));
    add(one, TagType::Short, "id")->integer = id;
    add(one, TagType::Short, "lvl")->integer = level;
}

QString enchantsOf(const ItemStack& item)
{
    QStringList parts;
    for (const auto& [id, level] : item.enchantments)
        parts << QStringLiteral("%1=%2").arg(id).arg(level);
    return parts.join(QLatin1Char(' '));
}
}

int main(int argc, char** argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    QCoreApplication app(argc, argv);

    {
        Tag stack(TagType::Compound);
        add(&stack, TagType::String, "id")->string = "minecraft:diamond_sword";
        add(&stack, TagType::Byte, "Count")->integer = 1;
        add(&stack, TagType::Byte, "Slot")->integer = 0;
        Tag* tag = add(&stack, TagType::Compound, "tag");
        add(tag, TagType::Int, "Damage")->integer = 41;
        Tag* display = add(tag, TagType::Compound, "display");
        add(display, TagType::String, "Name")->string = R"({"text":"Крушитель"})";
        Tag* ench = add(tag, TagType::List, "Enchantments");
        ench->listType = TagType::Compound;
        addEnchant(ench, "minecraft:sharpness", 5);
        addEnchant(ench, "minecraft:unbreaking", 3);

        const auto item = justnbt::readItemStack(stack);
        check(item.has_value(), QStringLiteral("Java 1.13-1.20.4: recognised"));
        if (item) {
            check(item->id == QLatin1String("minecraft:diamond_sword"), QStringLiteral("  id"));
            check(item->damage == 41 && item->maxDamage == 1561, QStringLiteral("  durability 1520 of 1561"));
            check(item->customName == QStringLiteral("Крушитель"), QStringLiteral("  name out of the JSON component"));
            check(enchantsOf(*item)
                      == QLatin1String("minecraft:sharpness=5 minecraft:unbreaking=3"),
                  QStringLiteral("  both enchantments"));
        }
    }

    {
        Tag stack(TagType::Compound);
        add(&stack, TagType::String, "id")->string = "minecraft:netherite_pickaxe";
        add(&stack, TagType::Int, "count")->integer = 1;
        Tag* components = add(&stack, TagType::Compound, "components");
        add(components, TagType::Int, "minecraft:damage")->integer = 58;
        Tag* ench = add(components, TagType::Compound, "minecraft:enchantments");
        add(ench, TagType::Int, "minecraft:efficiency")->integer = 5;
        Tag* name = add(components, TagType::Compound, "minecraft:custom_name");
        add(name, TagType::String, "text")->string = "Копалка";

        const auto item = justnbt::readItemStack(stack);
        check(item.has_value(), QStringLiteral("Java 1.20.5+: recognised"));
        if (item) {
            check(item->damage == 58 && item->maxDamage == 2031, QStringLiteral("  durability from the table"));
            check(item->customName == QStringLiteral("Копалка"), QStringLiteral("  name out of the NBT component"));
            check(enchantsOf(*item) == QLatin1String("minecraft:efficiency=5"), QStringLiteral("  enchantment"));
        }
    }

    {
        Tag stack(TagType::Compound);
        add(&stack, TagType::String, "id")->string = "minecraft:carrot_on_a_stick";
        add(&stack, TagType::Int, "count")->integer = 1;
        Tag* components = add(&stack, TagType::Compound, "components");
        add(components, TagType::Int, "minecraft:max_damage")->integer = 40;
        add(components, TagType::Int, "minecraft:damage")->integer = 5;
        Tag* ench = add(components, TagType::Compound, "minecraft:enchantments");
        Tag* levels = add(ench, TagType::Compound, "levels");
        add(levels, TagType::Int, "minecraft:unbreaking")->integer = 2;

        const auto item = justnbt::readItemStack(stack);
        check(item && item->maxDamage == 40 && item->damage == 5,
              QStringLiteral("Java: max_damage of the item wins over the table"));
        check(item && enchantsOf(*item) == QLatin1String("minecraft:unbreaking=2"),
              QStringLiteral("Java: enchantments inside \"levels\""));
    }

    {
        Tag stack(TagType::Compound);
        add(&stack, TagType::String, "Name")->string = "minecraft:diamond_pickaxe";
        add(&stack, TagType::Byte, "Count")->integer = 1;
        add(&stack, TagType::Short, "Damage")->integer = 100;
        Tag* tag = add(&stack, TagType::Compound, "tag");
        Tag* ench = add(tag, TagType::List, "ench");
        ench->listType = TagType::Compound;
        addEnchantNumeric(ench, 15, 4);
        addEnchantNumeric(ench, 17, 3);
        Tag* display = add(tag, TagType::Compound, "display");
        add(display, TagType::String, "Name")->string = "Кирка";

        const auto item = justnbt::readItemStack(stack);
        check(item.has_value(), QStringLiteral("Bedrock: recognised"));
        if (item) {
            check(item->bedrock, QStringLiteral("  marked as Bedrock"));
            check(item->damage == 100 && item->maxDamage == 1561, QStringLiteral("  durability 1461 of 1561"));
            check(item->customName == QStringLiteral("Кирка"), QStringLiteral("  plain custom name"));
            check(enchantsOf(*item) == QLatin1String("minecraft:efficiency=4 minecraft:unbreaking=3"),
                  QStringLiteral("  enchantment numbers of Bedrock"));
        }
    }

    {
        Tag stack(TagType::Compound);
        add(&stack, TagType::String, "id")->string = "minecraft:enchanted_book";
        add(&stack, TagType::Byte, "Count")->integer = 1;
        Tag* tag = add(&stack, TagType::Compound, "tag");
        Tag* ench = add(tag, TagType::List, "StoredEnchantments");
        ench->listType = TagType::Compound;
        addEnchant(ench, "minecraft:mending", 1);

        const auto item = justnbt::readItemStack(stack);
        check(item && item->storedEnchantments, QStringLiteral("Book: enchantments marked as stored"));
        check(item && !item->damageable(), QStringLiteral("Book: does not wear out"));
        const QString mending = item ? justnbt::enchantmentName(item->enchantments.front().first, 1) : QString();
        check(!mending.isEmpty() && !mending.endsWith(QLatin1String(" I")),
              QStringLiteral("Book: level I is not written out"));
        check(justnbt::enchantmentName(QStringLiteral("minecraft:unbreaking"), 3).endsWith(QLatin1String(" III")),
              QStringLiteral("Higher levels get a roman numeral"));
    }

    {
        Tag entity(TagType::Compound);
        add(&entity, TagType::String, "id")->string = "minecraft:zombie";
        add(&entity, TagType::Float, "Health")->floating = 20;
        check(!justnbt::readItemStack(entity).has_value(), QStringLiteral("An entity is not an item"));

        Tag blockEntity(TagType::Compound);
        add(&blockEntity, TagType::String, "id")->string = "minecraft:chest";
        add(&blockEntity, TagType::Int, "x")->integer = 10;
        add(&blockEntity, TagType::List, "Items");
        check(!justnbt::readItemStack(blockEntity).has_value(), QStringLiteral("A chest block entity is not an item"));

        Tag plain(TagType::Compound);
        add(&plain, TagType::Int, "Count")->integer = 3;
        check(!justnbt::readItemStack(plain).has_value(), QStringLiteral("A compound without an id is not an item"));
    }

    check(justnbt::defaultMaxDamage(QStringLiteral("minecraft:golden_hoe")) == 32, QStringLiteral("Durability: golden hoe"));
    check(justnbt::defaultMaxDamage(QStringLiteral("minecraft:diamond_chestplate")) == 528,
          QStringLiteral("Durability: diamond chestplate"));
    check(justnbt::defaultMaxDamage(QStringLiteral("minecraft:elytra")) == 432, QStringLiteral("Durability: elytra"));
    check(justnbt::defaultMaxDamage(QStringLiteral("minecraft:stone")) == 0, QStringLiteral("Durability: stone has none"));
    check(justnbt::prettifyId(QStringLiteral("minecraft:oak_planks")) == QStringLiteral("Oak planks"),
          QStringLiteral("Fallback name without Minecraft installed"));

    {
        Tag chunk(TagType::Compound);
        Tag* sections = add(&chunk, TagType::List, "sections");
        Tag* sec = sections->append(std::make_unique<Tag>(TagType::Compound));
        add(sec, TagType::Byte, "Y")->integer = -4;
        Tag* states = add(sec, TagType::Compound, "block_states");
        Tag* palette = add(states, TagType::List, "palette");
        Tag* stone = palette->append(std::make_unique<Tag>(TagType::Compound));
        add(stone, TagType::String, "Name")->string = "minecraft:stone";
        Tag* stairs = palette->append(std::make_unique<Tag>(TagType::Compound));
        add(stairs, TagType::String, "Name")->string = "minecraft:oak_stairs";
        Tag* props = add(stairs, TagType::Compound, "Properties");
        add(props, TagType::String, "facing")->string = "east";
        const auto s1 = justnbt::summarizeTag(*sec);
        check(s1 && s1->text.contains(QLatin1String("-64")) && s1->text.contains(QLatin1String("2")),
              QStringLiteral("a section: its heights and 2 kinds of blocks"));
        const auto s2 = justnbt::summarizeTag(*stairs);
        check(s2 && s2->iconId == "minecraft:oak_stairs" && s2->text.contains(QLatin1String("facing=east")),
              QStringLiteral("a palette block: its picture and properties"));

        Tag* blockEntities = add(&chunk, TagType::List, "block_entities");
        Tag* chest = blockEntities->append(std::make_unique<Tag>(TagType::Compound));
        add(chest, TagType::String, "id")->string = "minecraft:chest";
        add(chest, TagType::Int, "x")->integer = 12;
        add(chest, TagType::Int, "y")->integer = 64;
        add(chest, TagType::Int, "z")->integer = -3;
        Tag* items = add(chest, TagType::List, "Items");
        Tag* diamond = items->append(std::make_unique<Tag>(TagType::Compound));
        add(diamond, TagType::String, "id")->string = "minecraft:diamond";
        add(diamond, TagType::Int, "count")->integer = 5;
        add(diamond, TagType::Byte, "Slot")->integer = 0;
        const auto s3 = justnbt::summarizeTag(*chest);
        check(s3 && s3->iconId == "minecraft:chest" && s3->text.contains(QLatin1String("(12, 64, -3)")),
              QStringLiteral("a chest: its block and where it stands"));
        std::printf("      %s\n", qPrintable(s3 ? s3->text : QString()));
        check(justnbt::blockOfBlockEntity(QStringLiteral("MobSpawner")) == "minecraft:spawner"
                  && justnbt::blockOfBlockEntity(QStringLiteral("ShulkerBox")) == "minecraft:shulker_box",
              QStringLiteral("Bedrock block entities: MobSpawner, ShulkerBox"));

        Tag* entities = add(&chunk, TagType::List, "Entities");
        Tag* villager = entities->append(std::make_unique<Tag>(TagType::Compound));
        add(villager, TagType::String, "id")->string = "minecraft:villager";
        Tag* pos = add(villager, TagType::List, "Pos");
        pos->listType = TagType::Double;
        for (double v : {104.5, 70.0, -12.25})
            pos->append(std::make_unique<Tag>(TagType::Double))->floating = v;
        add(villager, TagType::Float, "Health")->floating = 20;
        add(villager, TagType::String, "CustomName")->string = "\"Bob\"";
        const auto s4 = justnbt::summarizeTag(*villager);
        check(s4 && s4->iconId.startsWith(QLatin1String("minecraft:villager")) && s4->text.contains(QLatin1String("Bob"))
                  && s4->text.contains(QStringLiteral("❤ 20")) && s4->text.contains(QLatin1String("(104.5, 70, -12.3)")),
              QStringLiteral("a villager: name, health, where"));
        std::printf("      %s\n", qPrintable(s4 ? s4->text : QString()));
        check(!justnbt::summarizeTag(*diamond) || justnbt::readItemStack(*diamond),
              QStringLiteral("an item is left to the item reader"));
    }
    std::printf("\n%d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
