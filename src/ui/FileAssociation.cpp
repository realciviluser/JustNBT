#include "ui/FileAssociation.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#endif

namespace justnbt {
namespace {
const QString kProgId = QStringLiteral("JustNBT.NBTFile");

QString appKey(const QString& exePath)
{
    return QStringLiteral("Applications/") + QFileInfo(exePath).fileName();
}

QString openCommand(const QString& exePath)
{
    return QLatin1Char('"') + QDir::toNativeSeparators(exePath) + QStringLiteral("\" \"%1\"");
}

QString iconValue(const QString& exePath)
{
    return QLatin1Char('"') + QDir::toNativeSeparators(exePath) + QStringLiteral("\",0");
}
}

QStringList nbtFileExtensions()
{
    return {QStringLiteral("dat"),       QStringLiteral("dat_old"), QStringLiteral("nbt"),
            QStringLiteral("schematic"), QStringLiteral("schem"),   QStringLiteral("litematic"),
            QStringLiteral("mcstructure")};
}

AssociationPlan associationPlan(const QString& exePath)
{
    AssociationPlan plan;
    const QString app = appKey(exePath);
    const QString command = openCommand(exePath);
    const QString icon = iconValue(exePath);
    const QString description = QCoreApplication::translate("FileAssociation", "NBT file");

    plan.entries << RegistryEntry{kProgId, QStringLiteral("Default"), description}
                 << RegistryEntry{kProgId + QStringLiteral("/DefaultIcon"), QStringLiteral("Default"), icon}
                 << RegistryEntry{kProgId + QStringLiteral("/shell/open/command"), QStringLiteral("Default"), command}
                 << RegistryEntry{app, QStringLiteral("FriendlyAppName"), QStringLiteral("JustNBT")}
                 << RegistryEntry{app + QStringLiteral("/DefaultIcon"), QStringLiteral("Default"), icon}
                 << RegistryEntry{app + QStringLiteral("/shell/open/command"), QStringLiteral("Default"), command};
    for (const QString& ext : nbtFileExtensions())
        plan.entries << RegistryEntry{app + QStringLiteral("/SupportedTypes"), QLatin1Char('.') + ext, QString()};
    plan.ownKeys << kProgId << app;

    for (const QString& ext : nbtFileExtensions())
        plan.sharedValues << RegistryEntry{QLatin1Char('.') + ext + QStringLiteral("/OpenWithProgids"), kProgId,
                                           QString()};
    return plan;
}

bool fileAssociationSupported()
{
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

#ifdef Q_OS_WIN

namespace {
QSettings& classes()
{
    static QSettings registry(QStringLiteral("HKEY_CURRENT_USER\\Software\\Classes"), QSettings::NativeFormat);
    return registry;
}
}

bool isFileAssociationSet(const QString& exePath)
{
    const QString stored = classes().value(kProgId + QStringLiteral("/shell/open/command/Default")).toString();
    return stored.compare(openCommand(exePath), Qt::CaseInsensitive) == 0;
}

bool setFileAssociation(bool on, const QString& exePath, QString* error)
{
    QSettings& registry = classes();
    const AssociationPlan plan = associationPlan(exePath);
    if (on) {
        for (const RegistryEntry& e : plan.entries + plan.sharedValues)
            registry.setValue(e.key + QLatin1Char('/') + e.name, e.value);
    } else {
        for (const QString& key : plan.ownKeys)
            registry.remove(key);
        for (const RegistryEntry& e : plan.sharedValues)
            registry.remove(e.key + QLatin1Char('/') + e.name);
    }
    registry.sync();
    if (registry.status() != QSettings::NoError) {
        if (error)
            *error = QCoreApplication::translate("FileAssociation", "Windows did not let JustNBT change the registry.");
        return false;
    }
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return true;
}

#else

bool isFileAssociationSet(const QString&)
{
    return false;
}

bool setFileAssociation(bool, const QString&, QString* error)
{
    if (error)
        *error = QCoreApplication::translate("FileAssociation", "Only available on Windows.");
    return false;
}

#endif
}
