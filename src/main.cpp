#include "core/Config.h"
#include "core/Maintenance.h"
#include "ui/MainWindow.h"
#include "ui/SingleInstance.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QIcon>
#include <QLibraryInfo>
#include <QTranslator>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("JustNBT"));
    QApplication::setApplicationName(QStringLiteral("JustNBT"));
    QApplication::setApplicationVersion(QStringLiteral(JUSTNBT_VERSION));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icon.ico")));

    const QString language = justnbt::uiLanguage();
    QTranslator appTranslator;
    QTranslator qtTranslator;
    if (appTranslator.load(QStringLiteral(":/i18n/justnbt_") + language))
        QApplication::installTranslator(&appTranslator);
    if (language != QLatin1String("en")) {
        const QString qtbase = QStringLiteral("qtbase_") + language;
        if (qtTranslator.load(qtbase, QApplication::applicationDirPath() + QStringLiteral("/translations"))
            || qtTranslator.load(qtbase, QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
            QApplication::installTranslator(&qtTranslator);
    }

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QCoreApplication::translate("main", "Editor for Minecraft worlds and NBT files"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("paths"),
                                 QCoreApplication::translate("main", "NBT files or world folders to open."),
                                 QStringLiteral("[paths...]"));
    parser.process(app);
    QStringList paths;
    for (const QString& path : parser.positionalArguments())
        paths << QFileInfo(path).absoluteFilePath();

    SingleInstance instance(justnbt::configPath());
    if (instance.handOver(paths))
        return 0;

    justnbt::settings().setValue(QStringLiteral("app/version"), QApplication::applicationVersion());
    justnbt::settings().sync();

    justnbt::startBackgroundMaintenance();

    MainWindow window;
    window.show();
    QObject::connect(&instance, &SingleInstance::pathsReceived, &window, [&window](const QStringList& received) {
        window.bringToFront();
        window.openPaths(received);
    });
    window.openPathsLater(paths);
    return app.exec();
}
