#include "core/Backup.h"
#include "core/Tr.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QSet>
#include <QStandardPaths>

#include <stdexcept>

namespace justnbt {
namespace {
QString makeBackupDir(const QString& subdir)
{
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss-zzz"));
    const QString dir = backupRoot() + '/' + subdir + '/' + stamp;
    if (!QDir().mkpath(dir))
        throw std::runtime_error((Tr::tr("Cannot create the backup folder: ") + dir).toStdString());
    return dir;
}
}

QString backupRoot()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/backups");
}

QString backupFile(const QString& file, const QString& subdir)
{
    const QString target = makeBackupDir(subdir) + '/' + QFileInfo(file).fileName();
    if (!QFile::copy(file, target))
        throw std::runtime_error(Tr::tr("The backup could not be made, the file was not saved").toStdString());
    return target;
}

QString backupFolderOncePerSession(const QString& folder, const QString& subdir)
{
    static QMutex mutex;
    static QSet<QString> done;
    const QString key = QDir(folder).absolutePath();

    QMutexLocker lock(&mutex);
    if (done.contains(key))
        return {};

    const QString target = makeBackupDir(subdir) + '/' + QDir(folder).dirName();
    const QDir source(folder);
    QDirIterator it(folder, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QString copy = target + '/' + source.relativeFilePath(path);
        QDir().mkpath(QFileInfo(copy).absolutePath());
        if (!QFile::copy(path, copy))
            throw std::runtime_error((Tr::tr("Cannot back up ") + path).toStdString());
    }
    done.insert(key);
    return target;
}
}
