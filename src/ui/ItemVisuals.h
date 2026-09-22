#pragma once

#include <QIcon>
#include <QPixmap>
#include <QString>

namespace justnbt {
QIcon itemIcon(const QString& id);

QPixmap itemPixmap(const QString& id, int size, qreal dpr);

QPixmap guiSprite(const QString& path, int scale, qreal dpr);

QPixmap texturePixmap(const QString& path, int scale, qreal dpr);

void clearItemIcons();
}
