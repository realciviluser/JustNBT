#pragma once

#include "core/Nbt.h"

#include <QString>
#include <QStringList>

#include <optional>
#include <utility>
#include <vector>

namespace justnbt {
struct ItemStack {
    QString id;
    QString customName;
    int count = 1;
    int damage = 0;
    int maxDamage = 0;
    bool bedrock = false;
    std::vector<std::pair<QString, int>> enchantments;
    bool storedEnchantments = false;

    bool damageable() const { return maxDamage > 0; }
};

std::optional<ItemStack> readItemStack(const nbt::Tag& tag);

QString itemDisplayName(const ItemStack& item);
QString enchantmentName(const QString& id, int level);
QString describeItem(const ItemStack& item);
QStringList describeItemLines(const ItemStack& item);

QString textComponentText(const nbt::Tag& tag);

int bedrockEnchantmentNumber(const QString& id);
QStringList knownEnchantments();

int defaultMaxDamage(const QString& id);
QString prettifyId(const QString& id);
}
