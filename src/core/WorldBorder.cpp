#include "core/WorldBorder.h"

#include "core/NbtFile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace justnbt {
namespace {
std::optional<double> number(const nbt::Tag* parent, const char* name)
{
    const nbt::Tag* t = parent ? parent->child(name) : nullptr;
    if (!t)
        return std::nullopt;
    if (nbt::isFloating(t->type))
        return t->floating;
    if (nbt::isInteger(t->type))
        return double(t->integer);
    return std::nullopt;
}

std::optional<WorldBorder> fromBorderFile(const QString& path)
{
    if (!QFile::exists(path))
        return std::nullopt;
    try {
        const auto file = NbtFile::load(path);
        const nbt::Tag* data = file->root->child("data");
        if (!data)
            data = file->root.get();
        const auto size = number(data, "size");
        if (!size)
            return std::nullopt;
        WorldBorder b;
        b.centerX = number(data, "center_x").value_or(0);
        b.centerZ = number(data, "center_z").value_or(0);
        b.size = *size;
        b.lerpTarget = number(data, "lerp_target").value_or(b.size);
        b.lerpTimeMs = int64_t(number(data, "lerp_time").value_or(0));
        b.sourceFile = path;
        return b;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<WorldBorder> fromLevelDat(const QString& path)
{
    try {
        const auto file = NbtFile::load(path);
        const nbt::Tag* data = file->root->child("Data");
        const auto size = number(data, "BorderSize");
        if (!size)
            return std::nullopt;
        WorldBorder b;
        b.centerX = number(data, "BorderCenterX").value_or(0);
        b.centerZ = number(data, "BorderCenterZ").value_or(0);
        b.size = *size;
        b.lerpTarget = number(data, "BorderSizeLerpTarget").value_or(b.size);
        b.lerpTimeMs = int64_t(number(data, "BorderSizeLerpTime").value_or(0));
        b.sourceFile = path;
        return b;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}
}

std::optional<WorldBorder> readWorldBorder(const WorldInfo& world, const Dimension& dimension)
{
    if (world.edition != Edition::Java)
        return std::nullopt;

    if (auto b = fromBorderFile(dimension.path + QStringLiteral("/data/minecraft/world_border.dat")))
        return b;
    if (auto b = fromBorderFile(dimension.path + QStringLiteral("/data/world_border.dat")))
        return b;

    if (world.layout == JavaLayout::BukkitLegacy && QDir(dimension.path) != QDir(world.path)) {
        const QString sibling = QFileInfo(dimension.path).absolutePath();
        if (auto b = fromLevelDat(sibling + QStringLiteral("/level.dat")))
            return b;
    }
    return fromLevelDat(world.path + QStringLiteral("/level.dat"));
}
}
