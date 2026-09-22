#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace justnbt {
enum class Edition { Java, Bedrock };

enum class JavaLayout {
    Legacy,
    BukkitLegacy,
    Modern,
};

struct Dimension {
    QString id;
    QString label;
    QString path;
    int bedrockId = -1;
};

struct WorldInfo {
    Edition edition = Edition::Java;
    JavaLayout layout = JavaLayout::Legacy;
    QString path;
    QString folderName;
    QString name;
    QString iconPath;
    QString serverKind;
    QDateTime lastPlayed;
    QList<Dimension> dimensions;
    QStringList siblingFolders;

    QString lockFilePath() const;
    QString description() const;
};

struct WorldFile {
    QString group;
    QString label;
    QString path;
    QByteArray dbKey;
};

QStringList defaultJavaRoots();
QStringList defaultBedrockRoots();

std::optional<WorldInfo> probeWorld(const QString& dir);
QList<WorldInfo> scanRoot(const QString& root);

QList<WorldFile> editableFiles(const WorldInfo& world);

QHash<QString, QString> playerNames(const WorldInfo& world);

bool isFileLocked(const QString& path);
bool isLockedByGame(const QString& lockPath);
inline bool isWorldInUse(const WorldInfo& w) { return isLockedByGame(w.lockFilePath()); }
}
