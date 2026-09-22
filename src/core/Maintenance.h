#pragma once

#include <QDateTime>
#include <QString>

namespace justnbt {
struct BackupPolicy {
    bool autoCleanup = true;
    int keepPerFile = 10;
    int keepDays = 7;
};

struct FolderStats {
    qint64 bytes = 0;
    int files = 0;
};

struct CleanupResult {
    int removed = 0;
    qint64 freedBytes = 0;
};

BackupPolicy loadBackupPolicy();
void saveBackupPolicy(const BackupPolicy& policy);
qint64 loadCacheLimitBytes();
void saveCacheLimitBytes(qint64 bytes);

FolderStats folderStats(const QString& dir);

CleanupResult cleanupBackups(const QString& root, const BackupPolicy& policy,
                             const QDateTime& now = QDateTime::currentDateTime());
CleanupResult removeAllBackups(const QString& root);

CleanupResult trimTileCache(const QString& root, qint64 limitBytes);
CleanupResult clearTileCache(const QString& root);

void startBackgroundMaintenance();
}
