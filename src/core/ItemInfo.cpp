#include "core/ItemInfo.h"
#include "core/Tr.h"

#include "core/GameAssets.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

namespace justnbt {
namespace {
using nbt::Tag;
using nbt::TagType;

const Tag* childOf(const Tag& tag, const char* name)
{
    return tag.child(name);
}

const Tag* stringChild(const Tag& tag, const char* name)
{
    const Tag* t = tag.child(name);
    return t && t->type == TagType::String ? t : nullptr;
}

const Tag* numberChild(const Tag& tag, const char* name)
{
    const Tag* t = tag.child(name);
    return t && nbt::isInteger(t->type) ? t : nullptr;
}

const Tag* compoundChild(const Tag& tag, const char* name)
{
    const Tag* t = tag.child(name);
    return t && t->type == TagType::Compound ? t : nullptr;
}

QString withNamespace(QString id)
{
    id = id.trimmed();
    return id.contains(':') ? id : QStringLiteral("minecraft:") + id;
}

QString plainText(const QString& raw)
{
    const QString text = raw.trimmed();
    if (!text.startsWith('{') && !text.startsWith('['))
        return raw;
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
    if (doc.isNull())
        return raw;
    QString out;
    auto walk = [&out](auto&& self, const QJsonValue& value) -> void {
        if (value.isString()) {
            out += value.toString();
        } else if (value.isArray()) {
            for (const QJsonValue& v : value.toArray())
                self(self, v);
        } else if (value.isObject()) {
            const QJsonObject o = value.toObject();
            out += o.value(QStringLiteral("text")).toString();
            if (o.contains(QStringLiteral("extra")))
                self(self, o.value(QStringLiteral("extra")));
        }
    };
    walk(walk, doc.isArray() ? QJsonValue(doc.array()) : QJsonValue(doc.object()));
    return out.isEmpty() ? raw : out;
}

QString nbtText(const Tag& tag)
{
    if (tag.type == TagType::String)
        return plainText(QString::fromStdString(tag.string));
    if (tag.type == TagType::List) {
        QString out;
        for (const auto& child : tag.children)
            out += nbtText(*child);
        return out;
    }
    if (tag.type != TagType::Compound)
        return {};
    QString out;
    if (const Tag* text = tag.child("text"); text && text->type == TagType::String)
        out += QString::fromStdString(text->string);
    if (const Tag* extra = tag.child("extra"))
        out += nbtText(*extra);
    return out;
}

QString roman(int n)
{
    if (n <= 0 || n > 3999)
        return QString::number(n);
    static const int values[] = {1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1};
    static const char* signs[] = {"M", "CM", "D", "CD", "C", "XC", "L", "XL", "X", "IX", "V", "IV", "I"};
    QString out;
    for (int i = 0; i < 13; ++i)
        while (n >= values[i]) {
            out += QLatin1String(signs[i]);
            n -= values[i];
        }
    return out;
}

const char* bedrockEnchantment(int id)
{
    static const char* names[] = {
        "protection", "fire_protection", "feather_falling", "blast_protection", "projectile_protection",
        "thorns", "respiration", "depth_strider", "aqua_affinity", "sharpness",
        "smite", "bane_of_arthropods", "knockback", "fire_aspect", "looting",
        "efficiency", "silk_touch", "unbreaking", "fortune", "power",
        "punch", "flame", "infinity", "luck_of_the_sea", "lure",
        "frost_walker", "mending", "binding_curse", "vanishing_curse", "impaling",
        "riptide", "loyalty", "channeling", "multishot", "piercing",
        "quick_charge", "soul_speed", "swift_sneak", "wind_burst", "density",
        "breach"};
    constexpr int count = int(sizeof(names) / sizeof(names[0]));
    return id >= 0 && id < count ? names[id] : nullptr;
}

const char* javaLegacyEnchantment(int id)
{
    static const QHash<int, const char*> names{
        {0, "protection"},     {1, "fire_protection"}, {2, "feather_falling"},  {3, "blast_protection"},
        {4, "projectile_protection"}, {5, "respiration"}, {6, "aqua_affinity"},  {7, "thorns"},
        {8, "depth_strider"},  {9, "frost_walker"},    {10, "binding_curse"},   {16, "sharpness"},
        {17, "smite"},         {18, "bane_of_arthropods"}, {19, "knockback"},   {20, "fire_aspect"},
        {21, "looting"},       {32, "efficiency"},     {33, "silk_touch"},      {34, "unbreaking"},
        {35, "fortune"},       {48, "power"},          {49, "punch"},           {50, "flame"},
        {51, "infinity"},      {61, "luck_of_the_sea"}, {62, "lure"},           {65, "loyalty"},
        {66, "impaling"},      {67, "riptide"},        {68, "channeling"},      {70, "mending"},
        {71, "vanishing_curse"}};
    return names.value(id, nullptr);
}

void addEnchantment(ItemStack& item, const QString& id, int level)
{
    if (!id.isEmpty())
        item.enchantments.emplace_back(withNamespace(id), level);
}

void readEnchantmentList(ItemStack& item, const Tag& list, bool numericIsBedrock)
{
    for (const auto& entry : list.children) {
        if (entry->type != TagType::Compound)
            continue;
        const Tag* lvl = numberChild(*entry, "lvl");
        const int level = lvl ? int(lvl->integer) : 1;
        if (const Tag* id = stringChild(*entry, "id")) {
            addEnchantment(item, QString::fromStdString(id->string), level);
        } else if (const Tag* number = numberChild(*entry, "id")) {
            const char* name = numericIsBedrock ? bedrockEnchantment(int(number->integer))
                                                : javaLegacyEnchantment(int(number->integer));
            addEnchantment(item, name ? QString::fromLatin1(name) : QString::number(number->integer), level);
        }
    }
}

void readEnchantmentComponent(ItemStack& item, const Tag& component)
{
    const Tag* levels = compoundChild(component, "levels");
    const Tag& map = levels ? *levels : component;
    for (const auto& entry : map.children)
        if (nbt::isInteger(entry->type))
            addEnchantment(item, QString::fromStdString(entry->name), int(entry->integer));
}
}

int bedrockEnchantmentNumber(const QString& id)
{
    const QString plain = id.mid(id.indexOf(':') + 1);
    for (int i = 0; bedrockEnchantment(i); ++i)
        if (plain == QLatin1String(bedrockEnchantment(i)))
            return i;
    return -1;
}

QStringList knownEnchantments()
{
    QStringList ids;
    for (int i = 0; bedrockEnchantment(i); ++i)
        ids << QStringLiteral("minecraft:") + QLatin1String(bedrockEnchantment(i));
    return ids;
}

QString textComponentText(const Tag& tag)
{
    return nbtText(tag);
}

int defaultMaxDamage(const QString& id)
{
    QString name = id;
    const int colon = name.indexOf(':');
    if (colon >= 0)
        name = name.mid(colon + 1);

    static const QHash<QString, int> special{
        {QStringLiteral("bow"), 384},        {QStringLiteral("crossbow"), 465},
        {QStringLiteral("trident"), 250},    {QStringLiteral("shield"), 336},
        {QStringLiteral("elytra"), 432},     {QStringLiteral("fishing_rod"), 64},
        {QStringLiteral("flint_and_steel"), 64}, {QStringLiteral("shears"), 238},
        {QStringLiteral("carrot_on_a_stick"), 25},
        {QStringLiteral("warped_fungus_on_a_stick"), 100},
        {QStringLiteral("brush"), 64},       {QStringLiteral("mace"), 500},
        {QStringLiteral("turtle_helmet"), 275}, {QStringLiteral("turtle_shell"), 275},
        {QStringLiteral("wolf_armor"), 64},  {QStringLiteral("sparkler"), 100},
        {QStringLiteral("glow_stick"), 100}};
    if (const auto it = special.constFind(name); it != special.constEnd())
        return *it;

    const int under = name.indexOf('_');
    if (under <= 0)
        return 0;
    const QString material = name.left(under);
    const QString piece = name.mid(under + 1);

    static const QHash<QString, int> toolDurability{
        {QStringLiteral("wooden"), 59},  {QStringLiteral("stone"), 131},    {QStringLiteral("golden"), 32},
        {QStringLiteral("iron"), 250},   {QStringLiteral("diamond"), 1561}, {QStringLiteral("netherite"), 2031}};
    if (piece == QLatin1String("sword") || piece == QLatin1String("pickaxe") || piece == QLatin1String("axe")
        || piece == QLatin1String("shovel") || piece == QLatin1String("hoe"))
        return toolDurability.value(material, 0);

    static const QHash<QString, QList<int>> armorDurability{
        {QStringLiteral("leather"), {55, 80, 75, 65}},   {QStringLiteral("chainmail"), {165, 240, 225, 195}},
        {QStringLiteral("iron"), {165, 240, 225, 195}},  {QStringLiteral("golden"), {77, 112, 105, 91}},
        {QStringLiteral("diamond"), {363, 528, 495, 429}}, {QStringLiteral("netherite"), {407, 592, 555, 481}}};
    const auto armor = armorDurability.constFind(material);
    if (armor == armorDurability.constEnd())
        return 0;
    if (piece == QLatin1String("helmet") || piece == QLatin1String("cap"))
        return armor->at(0);
    if (piece == QLatin1String("chestplate") || piece == QLatin1String("tunic"))
        return armor->at(1);
    if (piece == QLatin1String("leggings") || piece == QLatin1String("pants"))
        return armor->at(2);
    if (piece == QLatin1String("boots"))
        return armor->at(3);
    return 0;
}

QString prettifyId(const QString& id)
{
    QString name = id;
    const int colon = name.indexOf(':');
    if (colon >= 0)
        name = name.mid(colon + 1);
    name.replace('_', ' ');
    if (!name.isEmpty())
        name[0] = name[0].toUpper();
    return name;
}

std::optional<ItemStack> readItemStack(const Tag& tag)
{
    if (tag.type != TagType::Compound)
        return std::nullopt;

    ItemStack item;
    if (const Tag* id = stringChild(tag, "id")) {
        item.id = QString::fromStdString(id->string);
    } else if (const Tag* name = stringChild(tag, "Name")) {
        item.id = QString::fromStdString(name->string);
        item.bedrock = true;
    } else {
        return std::nullopt;
    }
    if (item.id.isEmpty())
        return std::nullopt;

    const Tag* count = numberChild(tag, "Count");
    if (!count)
        count = numberChild(tag, "count");
    const Tag* components = compoundChild(tag, "components");
    const Tag* itemTag = compoundChild(tag, "tag");
    const Tag* slot = numberChild(tag, "Slot");
    if (!count && !components && !(slot && itemTag))
        return std::nullopt;
    if (!count && numberChild(tag, "x") && numberChild(tag, "y") && numberChild(tag, "z"))
        return std::nullopt;

    item.id = withNamespace(item.id);
    item.count = count ? int(count->integer) : 1;
    item.maxDamage = defaultMaxDamage(item.id);

    if (item.bedrock) {
        if (const Tag* damage = numberChild(tag, "Damage"))
            item.damage = int(damage->integer);
    }
    if (itemTag) {
        if (const Tag* damage = numberChild(*itemTag, "Damage"))
            item.damage = int(damage->integer);
        if (const Tag* display = compoundChild(*itemTag, "display"))
            if (const Tag* name = display->child("Name"))
                item.customName = nbtText(*name);
        if (const Tag* ench = childOf(*itemTag, "Enchantments"); ench && ench->type == TagType::List)
            readEnchantmentList(item, *ench, false);
        if (const Tag* ench = childOf(*itemTag, "StoredEnchantments"); ench && ench->type == TagType::List) {
            readEnchantmentList(item, *ench, false);
            item.storedEnchantments = true;
        }
        if (const Tag* ench = childOf(*itemTag, "ench"); ench && ench->type == TagType::List)
            readEnchantmentList(item, *ench, item.bedrock);
    }
    if (components) {
        if (const Tag* damage = numberChild(*components, "minecraft:damage"))
            item.damage = int(damage->integer);
        if (const Tag* maxDamage = numberChild(*components, "minecraft:max_damage"))
            item.maxDamage = int(maxDamage->integer);
        for (const char* key : {"minecraft:custom_name", "minecraft:item_name"}) {
            if (!item.customName.isEmpty())
                break;
            if (const Tag* name = components->child(key))
                item.customName = nbtText(*name);
        }
        if (const Tag* ench = compoundChild(*components, "minecraft:enchantments"))
            readEnchantmentComponent(item, *ench);
        if (const Tag* ench = compoundChild(*components, "minecraft:stored_enchantments")) {
            readEnchantmentComponent(item, *ench);
            item.storedEnchantments = true;
        }
    }
    return item;
}

QString itemDisplayName(const ItemStack& item)
{
    if (!item.customName.isEmpty())
        return item.customName;
    QString name = item.id;
    const int colon = name.indexOf(':');
    const QString space = colon >= 0 ? name.left(colon) : QStringLiteral("minecraft");
    const QString key = colon >= 0 ? name.mid(colon + 1) : name;
    const GameAssets& assets = GameAssets::instance();
    for (const char* kind : {"item", "block"}) {
        const QString text = assets.translate(QStringLiteral("%1.%2.%3").arg(QLatin1String(kind), space, key));
        if (!text.isEmpty())
            return text;
    }
    return prettifyId(item.id);
}

QString enchantmentName(const QString& id, int level)
{
    QString name = id;
    const int colon = name.indexOf(':');
    const QString space = colon >= 0 ? name.left(colon) : QStringLiteral("minecraft");
    const QString key = colon >= 0 ? name.mid(colon + 1) : name;
    QString text = GameAssets::instance().translate(QStringLiteral("enchantment.%1.%2").arg(space, key));
    if (text.isEmpty())
        text = prettifyId(id);
    return level == 1 ? text : text + QChar(' ') + roman(level);
}

QStringList describeItemLines(const ItemStack& item)
{
    QStringList lines;
    lines << itemDisplayName(item);
    if (!item.customName.isEmpty())
        lines << Tr::tr("Normally: %1").arg(prettifyId(item.id));
    lines << item.id;
    if (item.count != 1)
        lines << Tr::tr("Count: %1").arg(item.count);
    if (item.damageable())
        lines << Tr::tr("Durability: %1 of %2").arg(item.maxDamage - item.damage).arg(item.maxDamage);
    else if (item.damage != 0)
        lines << QStringLiteral("Damage: %1").arg(item.damage);
    if (!item.enchantments.empty()) {
        QStringList names;
        for (const auto& [id, level] : item.enchantments)
            names << enchantmentName(id, level);
        lines << (item.storedEnchantments ? Tr::tr("Stored enchantments: ") : Tr::tr("Enchantments: "))
                + names.join(QStringLiteral(", "));
    }
    return lines;
}

QString describeItem(const ItemStack& item)
{
    QStringList parts;
    QString head = itemDisplayName(item);
    if (item.count != 1)
        head += QStringLiteral(" ×%1").arg(item.count);
    parts << head;
    if (item.damageable())
        parts << QStringLiteral("%1/%2").arg(item.maxDamage - item.damage).arg(item.maxDamage);
    if (!item.enchantments.empty()) {
        QStringList names;
        for (const auto& [id, level] : item.enchantments)
            names << enchantmentName(id, level);
        parts << names.join(QStringLiteral(", "));
    }
    return parts.join(QStringLiteral(" · "));
}
}
