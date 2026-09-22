#pragma once

#include "core/Nbt.h"

#include <QString>
#include <QStringList>

#include <optional>

namespace justnbt {
struct TagSummary {
    QString iconId;
    QString text;
    QStringList tooltip;
};

std::optional<TagSummary> summarizeTag(const nbt::Tag& tag);

QString blockOfBlockEntity(const QString& id);
}
