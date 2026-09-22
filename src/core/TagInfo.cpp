#include "core/TagInfo.h"
#include "core/Tr.h"

#include "core/GameAssets.h"
#include "core/ItemInfo.h"

#include <QHash>

#include <cmath>

namespace justnbt {
namespace {
using nbt::Tag;
using nbt::TagType;

QString stringOf(const Tag& t, const char* name)
{
    const Tag* c = t.child(name);
    return c && c->type == TagType::String ? QString::fromStdString(c->string) : QString();
}

const Tag* numberChild(const Tag& t, const char* name)
{
    const Tag* c = t.child(name);
    return c && (nbt::isInteger(c->type) || nbt::isFloating(c->type)) ? c : nullptr;
}

double numberOf(const Tag* t)
{
    return nbt::isFloating(t->type) ? t->floating : double(t->integer);
}

QString withNamespace(const QString& id)
{
    return id.contains(QLatin1Char(':')) ? id : QStringLiteral("minecraft:") + id;
}

QString gameName(const char* kind, const QString& id)
{
    const QString full = withNamespace(id);
    const int colon = full.indexOf(QLatin1Char(':'));
    const QString text = GameAssets::instance().translate(
        QStringLiteral("%1.%2.%3").arg(QLatin1String(kind), full.left(colon), full.mid(colon + 1)));
    return text.isEmpty() ? prettifyId(full) : text;
}

bool hasTexture(const QString& id)
{
    return !GameAssets::instance().texturePng(id).isEmpty();
}

QString number(double v)
{
    const double r = std::round(v * 10) / 10;
    return QString::number(r, 'f', r == std::floor(r) ? 0 : 1);
}

QStringList properties(const Tag* props)
{
    QStringList out;
    if (!props || props->type != TagType::Compound)
        return out;
    for (const auto& p : props->children) {
        QString value;
        if (p->type == TagType::String)
            value = QString::fromStdString(p->string);
        else if (nbt::isInteger(p->type))
            value = QString::number(p->integer);
        else
            continue;
        out << QString::fromStdString(p->name) + QLatin1Char('=') + value;
    }
    return out;
}

std::optional<TagSummary> blockState(const Tag& tag)
{
    QString id = stringOf(tag, "Name");
    const Tag* props = tag.child("Properties");
    if (id.isEmpty()) {
        id = stringOf(tag, "name");
        props = tag.child("states");
    }
    if (id.isEmpty())
        return std::nullopt;
    const bool inPalette = tag.parent && (tag.parent->name == "palette" || tag.parent->name == "Palette");
    if (!inPalette && !props)
        return std::nullopt;
    TagSummary s;
    s.iconId = withNamespace(id);
    const QStringList p = properties(props);
    s.text = gameName("block", id);
    if (!p.isEmpty())
        s.text += QStringLiteral(" · ") + p.join(QStringLiteral(", "));
    s.tooltip << gameName("block", id) << withNamespace(id) << p;
    return s;
}

int itemCount(const Tag& tag)
{
    const Tag* items = tag.child("Items");
    if (!items || items->type != TagType::List)
        return -1;
    int n = 0;
    for (const auto& c : items->children)
        if (const auto stack = readItemStack(*c); stack && stack->count > 0)
            ++n;
    return n;
}

QString spawnerMob(const Tag& tag)
{
    if (const Tag* data = tag.child("SpawnData"); data && data->type == TagType::Compound) {
        if (const Tag* entity = data->child("entity"); entity && entity->type == TagType::Compound)
            if (const QString id = stringOf(*entity, "id"); !id.isEmpty())
                return id;
        if (const QString id = stringOf(*data, "id"); !id.isEmpty())
            return id;
    }
    return stringOf(tag, "EntityIdentifier");
}

std::optional<TagSummary> blockEntity(const Tag& tag)
{
    const QString id = stringOf(tag, "id");
    const Tag *x = numberChild(tag, "x"), *y = numberChild(tag, "y"), *z = numberChild(tag, "z");
    if (id.isEmpty() || !x || !y || !z || tag.child("Pos"))
        return std::nullopt;
    TagSummary s;
    const QString block = blockOfBlockEntity(id);
    s.iconId = block;
    const QString name = gameName("block", block);
    const QString where = QStringLiteral("(%1, %2, %3)").arg(x->integer).arg(y->integer).arg(z->integer);
    QStringList extra;
    if (const Tag* custom = tag.child("CustomName"))
        if (const QString text = textComponentText(*custom); !text.isEmpty())
            extra << QStringLiteral("«%1»").arg(text);
    if (const int n = itemCount(tag); n >= 0)
        extra << Tr::tr("%n item(s)", nullptr, n);
    if (const QString mob = spawnerMob(tag); !mob.isEmpty())
        extra << Tr::tr("Spawner: %1").arg(gameName("entity", mob));
    s.text = name + QLatin1Char(' ') + where;
    if (!extra.isEmpty())
        s.text += QStringLiteral(" · ") + extra.join(QStringLiteral(" · "));
    s.tooltip << name << withNamespace(id) << where << extra;
    return s;
}

QString entityIcon(const QString& id)
{
    const QString full = withNamespace(id);
    const QString egg = full + QStringLiteral("_spawn_egg");
    if (hasTexture(egg))
        return egg;
    if (hasTexture(full))
        return full;
    return egg;
}

std::optional<TagSummary> entity(const Tag& tag)
{
    QString id = stringOf(tag, "id");
    if (id.isEmpty())
        id = stringOf(tag, "identifier");
    const Tag* pos = tag.child("Pos");
    if (id.isEmpty() || !pos || pos->type != TagType::List || pos->children.size() < 3)
        return std::nullopt;
    TagSummary s;
    const QString name = gameName("entity", id);
    const QString where = QStringLiteral("(%1, %2, %3)")
                              .arg(number(numberOf(pos->children[0].get())), number(numberOf(pos->children[1].get())),
                                   number(numberOf(pos->children[2].get())));
    QStringList extra;
    s.iconId = entityIcon(id);
    if (const Tag* item = tag.child("Item"); item && item->type == TagType::Compound)
        if (const auto stack = readItemStack(*item)) {
            extra << describeItem(*stack);
            if (withNamespace(id) == QLatin1String("minecraft:item"))
                s.iconId = stack->id;
        }
    if (const Tag* custom = tag.child("CustomName"))
        if (const QString text = textComponentText(*custom); !text.isEmpty())
            extra.prepend(QStringLiteral("«%1»").arg(text));
    if (const Tag* data = tag.child("VillagerData"); data && data->type == TagType::Compound) {
        const QString profession = stringOf(*data, "profession");
        const QString path = profession.mid(profession.indexOf(QLatin1Char(':')) + 1);
        if (!path.isEmpty() && path != QLatin1String("none")) {
            const QString text = GameAssets::instance().translate(QStringLiteral("entity.minecraft.villager.") + path);
            extra << (text.isEmpty() ? prettifyId(path) : text);
        }
    }
    double health = -1;
    if (const Tag* h = numberChild(tag, "Health"))
        health = numberOf(h);
    else if (const Tag* attributes = tag.child("Attributes"); attributes && attributes->type == TagType::List)
        for (const auto& a : attributes->children)
            if (a->type == TagType::Compound && stringOf(*a, "Name") == QLatin1String("minecraft:health"))
                if (const Tag* current = numberChild(*a, "Current"))
                    health = numberOf(current);
    if (health >= 0)
        extra << QStringLiteral("❤ ") + number(health);
    s.text = name;
    if (!extra.isEmpty())
        s.text += QStringLiteral(" · ") + extra.join(QStringLiteral(" · "));
    s.text += QLatin1Char(' ') + where;
    s.tooltip << name << withNamespace(id) << where << extra;
    return s;
}

std::optional<TagSummary> section(const Tag& tag)
{
    const Tag* y = numberChild(tag, "Y");
    if (!y || !tag.parent || (tag.parent->name != "sections" && tag.parent->name != "Sections"))
        return std::nullopt;
    const Tag* palette = nullptr;
    if (const Tag* states = tag.child("block_states"); states && states->type == TagType::Compound)
        palette = states->child("palette");
    if (!palette)
        palette = tag.child("Palette");
    if (const Tag* layers = tag.child("layers"); !palette && layers && layers->type == TagType::List && !layers->children.empty())
        palette = layers->children.front()->child("palette");
    TagSummary s;
    const int sy = int(y->integer);
    s.text = Tr::tr("Y %1 · heights %2…%3").arg(sy).arg(sy * 16).arg(sy * 16 + 15);
    if (palette && palette->type == TagType::List) {
        QStringList kinds;
        for (const auto& p : palette->children) {
            QString id = stringOf(*p, "Name");
            if (id.isEmpty())
                id = stringOf(*p, "name");
            if (!id.isEmpty() && !kinds.contains(id))
                kinds << id;
        }
        const bool empty = kinds.isEmpty() || (kinds.size() == 1 && kinds.front().endsWith(QLatin1String(":air")));
        s.text += QStringLiteral(" · ") + (empty ? Tr::tr("only air") : Tr::tr("%n kind(s) of blocks", nullptr, int(kinds.size())));
        for (const QString& k : std::as_const(kinds).mid(0, 30))
            s.tooltip << gameName("block", k);
    }
    s.tooltip.prepend(s.text);
    return s;
}
}

QString blockOfBlockEntity(const QString& id)
{
    static const QHash<QString, QString> aliases{
        {QStringLiteral("mob_spawner"), QStringLiteral("spawner")},
        {QStringLiteral("enchant_table"), QStringLiteral("enchanting_table")},
        {QStringLiteral("sign"), QStringLiteral("oak_sign")},
        {QStringLiteral("hanging_sign"), QStringLiteral("oak_hanging_sign")},
        {QStringLiteral("bed"), QStringLiteral("red_bed")},
        {QStringLiteral("banner"), QStringLiteral("white_banner")},
        {QStringLiteral("skull"), QStringLiteral("skeleton_skull")},
        {QStringLiteral("music"), QStringLiteral("note_block")},
        {QStringLiteral("noteblock"), QStringLiteral("note_block")},
        {QStringLiteral("piston_arm"), QStringLiteral("piston")},
        {QStringLiteral("brushable_block"), QStringLiteral("suspicious_sand")},
        {QStringLiteral("chalkboard_block"), QStringLiteral("oak_sign")},
        {QStringLiteral("end_gateway"), QStringLiteral("end_gateway")},
        {QStringLiteral("trapped_chest"), QStringLiteral("trapped_chest")},
        {QStringLiteral("jigsaw_block"), QStringLiteral("jigsaw")},
        {QStringLiteral("structure_block"), QStringLiteral("structure_block")}};
    QString path = id.mid(id.indexOf(QLatin1Char(':')) + 1);
    if (!path.isEmpty() && path.at(0).isUpper()) {
        QString snake;
        for (int i = 0; i < path.size(); ++i) {
            if (path[i].isUpper() && i > 0 && path[i - 1].isLower())
                snake += QLatin1Char('_');
            snake += path[i].toLower();
        }
        path = snake;
    }
    return QStringLiteral("minecraft:") + aliases.value(path, path);
}

std::optional<TagSummary> summarizeTag(const Tag& tag)
{
    if (tag.type != TagType::Compound)
        return std::nullopt;
    if (auto s = section(tag))
        return s;
    if (auto s = entity(tag))
        return s;
    if (auto s = blockEntity(tag))
        return s;
    return blockState(tag);
}
}
