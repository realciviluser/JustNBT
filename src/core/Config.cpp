#include "core/Config.h"

#include <QDir>
#include <QLocale>
#include <QStandardPaths>

namespace {
const QString kLanguageKey = QStringLiteral("ui/language");
}

namespace justnbt {
QString configPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/config.ini");
}

QSettings& settings()
{
    static QSettings instance(configPath(), QSettings::IniFormat);
    return instance;
}

QString configuredLanguage()
{
    const QString value = settings().value(kLanguageKey, QStringLiteral("auto")).toString().toLower();
    return value == QLatin1String("ru") || value == QLatin1String("en") ? value : QStringLiteral("auto");
}

void setConfiguredLanguage(const QString& language)
{
    settings().setValue(kLanguageKey, language);
    settings().sync();
}

QString uiLanguage()
{
    const QString configured = configuredLanguage();
    if (configured != QLatin1String("auto"))
        return configured;
    return QLocale::system().name().startsWith(QLatin1String("ru"), Qt::CaseInsensitive) ? QStringLiteral("ru")
                                                                                         : QStringLiteral("en");
}
}
