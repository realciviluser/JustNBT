#pragma once

#include "core/Worlds.h"

#include <optional>

namespace justnbt {
struct WorldBorder {
    double centerX = 0;
    double centerZ = 0;
    double size = 59999968;
    double lerpTarget = 59999968;
    int64_t lerpTimeMs = 0;
    QString sourceFile;

    double minX() const { return centerX - size / 2; }
    double maxX() const { return centerX + size / 2; }
    double minZ() const { return centerZ - size / 2; }
    double maxZ() const { return centerZ + size / 2; }
    bool contains(double x, double z) const { return x >= minX() && x < maxX() && z >= minZ() && z < maxZ(); }
};

std::optional<WorldBorder> readWorldBorder(const WorldInfo& world, const Dimension& dimension);
}
