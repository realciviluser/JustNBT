#include "core/Maintenance.h"

#include "core/Backup.h"
#include "core/Config.h"
#include "core/MapRender.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QThreadPool>

#include <algorithm>

namespace justnbt {
namespace {
const QString kStampFormat = QStringLiteral("yyyy-MM-dd_HH-mm-ss-zzz");

bool isInside(const QString& path, const QString& root)
{
    const QString p = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    const QString r = QDir::cleanPath(QDir(root).absolutePath());
#ifdef Q_OS_WIN
    return p.startsWith(r + '/', Qt::CaseInsensitive);
#else
    return p.startsWith(r + '/', Qt::CaseSensitive);
#endif
}

qint64 sizeOf(const QString& path)
{
    const QFileInfo fi(path);
    if (fi.isFile())
        return fi.size();
    return folderStats(path).bytes;
}

bool removePath(const QString& path, const QString& root)
{
    if (!isInside(path, root))
        return false;
    const QFileInfo fi(path);
    return fi.isDir() ? QDir(path).removeRecursively() : QFile::remove(path);
}

void removeEmptyParents(QString dir, const QString& root)
{
    while (isInside(dir, root)) {
        QDir d(dir);
        if (!d.isEmpty())
            break;
        const QString parent = QFileInfo(dir).absolutePath();
        d.rmdir(dir);
        dir = parent;
    }
}

struct BackupItem {
    QString path;
    QString stampDir;
    QDateTime time;
};
}

BackupPolicy loadBackupPolicy()
{
    QSettings& s = settings();
    BackupPolicy p;
    p.autoCleanup = s.value(QStringLiteral("backups/autoCleanup"), p.autoCleanup).toBool();
    p.keepPerFile = std::max(1, s.value(QStringLiteral("backups/keepPerFile"), p.keepPerFile).toInt());
    p.keepDays = std::max(0, s.value(QStringLiteral("backups/keepDays"), p.keepDays).toInt());
    return p;
}

void saveBackupPolicy(const BackupPolicy& policy)
{
    QSettings& s = settings();
    s.setValue(QStringLiteral("backups/autoCleanup"), policy.autoCleanup);
    s.setValue(QStringLiteral("backups/keepPerFile"), std::max(1, policy.keepPerFile));
    s.setValue(QStringLiteral("backups/keepDays"), std::max(0, policy.keepDays));
}

qint64 loadCacheLimitBytes()
{
    return settings().value(QStringLiteral("cache/limitBytes"), qint64(512) << 20).toLongLong();
}

void saveCacheLimitBytes(qint64 bytes)
{
    settings().setValue(QStringLiteral("cache/limitBytes"), std::max<qint64>(bytes, 16 << 20));
}

FolderStats folderStats(const QString& dir)
{
    FolderStats st;
    QDirIterator it(dir, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        st.bytes += it.fileInfo().size();
        ++st.files;
    }
    return st;
}

CleanupResult cleanupBackups(const QString& root, const BackupPolicy& policy, const QDateTime& now)
{
    static const QRegularExpression stampRe(QStringLiteral("^\\d{4}-\\d{2}-\\d{2}_\\d{2}-\\d{2}-\\d{2}-\\d{3}$"));

    QHash<QString, std::vector<BackupItem>> groups;
    QDirIterator dirs(root, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (dirs.hasNext()) {
        const QString stampDir = dirs.next();
        const QString name = dirs.fileName();
        if (!stampRe.match(name).hasMatch())
            continue;
        const QDateTime time = QDateTime::fromString(name, kStampFormat);
        if (!time.isValid())
            continue;
        const QString parentRel = QDir(root).relativeFilePath(QFileInfo(stampDir).absolutePath());
        for (const QFileInfo& entry : QDir(stampDir).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot))
            groups[parentRel + '/' + entry.fileName()].push_back({entry.absoluteFilePath(), stampDir, time});
    }

    CleanupResult result;
    const QDateTime cutoff = now.addDays(-policy.keepDays);
    const int keep = std::max(1, policy.keepPerFile);
    for (auto& items : groups) {
        std::sort(items.begin(), items.end(), [](const BackupItem& a, const BackupItem& b) { return a.time > b.time; });
        for (size_t i = size_t(keep); i < items.size(); ++i) {
            const BackupItem& item = items[i];
            if (item.time >= cutoff)
                continue;
            const qint64 size = sizeOf(item.path);
            if (removePath(item.path, root)) {
                ++result.removed;
                result.freedBytes += size;
                removeEmptyParents(item.stampDir, root);
            }
        }
    }
    return result;
}

CleanupResult removeAllBackups(const QString& root)
{
    CleanupResult result;
    for (const QFileInfo& fi : QDir(root).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
        const FolderStats st = fi.isDir() ? folderStats(fi.absoluteFilePath()) : FolderStats{fi.size(), 1};
        if (removePath(fi.absoluteFilePath(), root)) {
            result.removed += st.files;
            result.freedBytes += st.bytes;
        }
    }
    return result;
}

CleanupResult trimTileCache(const QString& root, qint64 limitBytes)
{
    std::vector<QFileInfo> files;
    qint64 total = 0;
    QDirIterator it(root, {QStringLiteral("*.jnt")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        files.push_back(it.fileInfo());
        total += it.fileInfo().size();
    }
    CleanupResult result;
    if (total <= limitBytes)
        return result;
    std::sort(files.begin(), files.end(),
              [](const QFileInfo& a, const QFileInfo& b) { return a.lastModified() < b.lastModified(); });
    for (const QFileInfo& fi : files) {
        if (total <= limitBytes)
            break;
        if (removePath(fi.absoluteFilePath(), root)) {
            total -= fi.size();
            result.freedBytes += fi.size();
            ++result.removed;
            removeEmptyParents(fi.absolutePath(), root);
        }
    }
    return result;
}

CleanupResult clearTileCache(const QString& root)
{
    return trimTileCache(root, 0);
}

void startBackgroundMaintenance()
{
    const BackupPolicy policy = loadBackupPolicy();
    const qint64 cacheLimit = loadCacheLimitBytes();
    const QString backups = backupRoot();
    const QString cache = tileCacheRoot();
    QThreadPool::globalInstance()->start([=] {
        if (policy.autoCleanup)
            cleanupBackups(backups, policy);
        trimTileCache(cache, cacheLimit);
    });
}
}
