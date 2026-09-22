#pragma once

#include <QString>

namespace justnbt {
QString backupRoot();

QString backupFile(const QString& file, const QString& subdir);

QString backupFolderOncePerSession(const QString& folder, const QString& subdir);
}
