#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

#include <memory>

namespace justnbt {
class GameAssets {
public:
    static GameAssets& instance();

    bool available() const;
    QString versionName() const;
    QString languageCode() const;
    QString jarPath() const;
    QString describeSource() const;

    QString translate(const QString& key) const;
    QList<QPair<QString, QString>> namedIds(const QString& kind) const;
    QByteArray texturePng(const QString& id) const;
    QByteArray textureFile(const QString& path) const;

    void reload();

    GameAssets(const GameAssets&) = delete;
    GameAssets& operator=(const GameAssets&) = delete;

private:
    GameAssets();
    ~GameAssets();
    struct Impl;
    std::unique_ptr<Impl> d;
};

struct MinecraftVersion {
    QString id;
    QString jar;
    QString assetsDir;
    QString assetIndex;
    QDateTime released;
    bool release = false;
};

QList<MinecraftVersion> versionsIn(const QString& dir);
QString defaultMinecraftDir();
QString minecraftDir();
QString resolveMinecraftDir(const QString& dir);
QString configuredMinecraftVersion();
void setMinecraftSource(const QString& dir, const QString& version);
}
