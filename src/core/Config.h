#pragma once

#include <QSettings>
#include <QString>

namespace justnbt {
QString configPath();
QSettings& settings();

QString uiLanguage();
QString configuredLanguage();
void setConfiguredLanguage(const QString& language);
}
