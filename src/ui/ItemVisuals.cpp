#include "ui/ItemVisuals.h"

#include "core/BlockColors.h"
#include "core/GameAssets.h"

#include <QHash>
#include <QImage>
#include <QPainter>
#include <QPixmap>

namespace justnbt {
namespace {
constexpr int kSize = 16;

QImage firstFrame(QImage image)
{
    if (image.width() > 0 && image.height() > image.width() && image.height() % image.width() == 0)
        image = image.copy(0, 0, image.width(), image.width());
    return image;
}

QIcon fallbackIcon(const QString& id)
{
    QString name = id;
    const int colon = name.indexOf(':');
    if (colon >= 0)
        name = name.mid(colon + 1);
    const std::string key = name.toStdString();
    const BlockStyle style = blockStyle(key, false);
    const QColor color = style.known ? QColor(QRgb(0xff000000u | style.rgb)) : QColor(0x88, 0x8b, 0x90);

    constexpr int scale = 2;
    QPixmap pm(kSize * scale, kSize * scale);
    pm.fill(Qt::transparent);
    {
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(scale, scale);
        p.setPen(QPen(color.darker(140), 1));
        p.setBrush(color);
        p.drawRoundedRect(QRectF(1.5, 1.5, kSize - 3, kSize - 3), 2, 2);
    }
    pm.setDevicePixelRatio(scale);
    return QIcon(pm);
}

QImage itemImage(const QString& id)
{
    QImage image;
    if (id == QLatin1String("minecraft:shield")) {
        const QByteArray png = GameAssets::instance().textureFile(QStringLiteral("entity/shield_base_nopattern"));
        if (!png.isEmpty() && image.loadFromData(png, "PNG") && image.width() >= 14 && image.height() >= 24) {
            const QImage front = image.copy(1, 1, 12, 22).scaled(9, 16, Qt::IgnoreAspectRatio, Qt::FastTransformation);
            QImage square(16, 16, QImage::Format_ARGB32);
            square.fill(Qt::transparent);
            QPainter p(&square);
            p.drawImage(4, 0, front);
            return square;
        }
    }
    const QByteArray png = GameAssets::instance().texturePng(id);
    if (png.isEmpty() || !image.loadFromData(png, "PNG"))
        return {};
    return firstFrame(image);
}

QIcon buildIcon(const QString& id)
{
    const QImage image = itemImage(id);
    if (image.isNull())
        return fallbackIcon(id);

    QIcon icon;
    for (int scale : {1, 2}) {
        QPixmap pm = QPixmap::fromImage(
            image.scaled(kSize * scale, kSize * scale, Qt::KeepAspectRatio, Qt::FastTransformation));
        pm.setDevicePixelRatio(scale);
        icon.addPixmap(pm);
    }
    return icon;
}

QHash<QString, QPixmap>& pixmapCache()
{
    static QHash<QString, QPixmap> pixmaps;
    return pixmaps;
}

QHash<QString, QIcon>& cache()
{
    static QHash<QString, QIcon> icons;
    return icons;
}
}

QIcon itemIcon(const QString& id)
{
    auto& icons = cache();
    const auto it = icons.constFind(id);
    if (it != icons.constEnd())
        return *it;
    return *icons.insert(id, buildIcon(id));
}

QPixmap itemPixmap(const QString& id, int size, qreal dpr)
{
    const QString key = QStringLiteral("item|%1|%2|%3").arg(id).arg(size).arg(dpr);
    auto& pixmaps = pixmapCache();
    if (const auto it = pixmaps.constFind(key); it != pixmaps.constEnd())
        return *it;
    const QImage image = itemImage(id);
    QPixmap pm;
    if (!image.isNull()) {
        const int px = qRound(size * dpr);
        pm = QPixmap::fromImage(image.scaled(px, px, Qt::KeepAspectRatio, Qt::FastTransformation));
        pm.setDevicePixelRatio(dpr);
    } else {
        pm = itemIcon(id).pixmap(QSize(size, size), dpr);
    }
    return *pixmaps.insert(key, pm);
}

QPixmap guiSprite(const QString& path, int scale, qreal dpr)
{
    return texturePixmap(QStringLiteral("gui/sprites/") + path, scale, dpr);
}

QPixmap texturePixmap(const QString& path, int scale, qreal dpr)
{
    const QString key = QStringLiteral("texture|%1|%2|%3").arg(path).arg(scale).arg(dpr);
    auto& pixmaps = pixmapCache();
    if (const auto it = pixmaps.constFind(key); it != pixmaps.constEnd())
        return *it;
    QImage image;
    const QByteArray png = GameAssets::instance().textureFile(path);
    QPixmap pm;
    if (!png.isEmpty() && image.loadFromData(png, "PNG")) {
        pm = QPixmap::fromImage(image.scaled(qRound(image.width() * scale * dpr), qRound(image.height() * scale * dpr),
                                             Qt::IgnoreAspectRatio, Qt::FastTransformation));
        pm.setDevicePixelRatio(dpr);
    }
    return *pixmaps.insert(key, pm);
}

void clearItemIcons()
{
    cache().clear();
    pixmapCache().clear();
}
}
