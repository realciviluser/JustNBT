#include "ui/MaintenanceDialog.h"

#include <QApplication>
#include <QLibraryInfo>
#include <QPixmap>
#include <QTranslator>

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("JustNBT"));
    QApplication::setApplicationName(QStringLiteral("JustNBT"));
    if (argc < 2)
        return 2;
    QTranslator qt;
    if (qt.load(QStringLiteral("qtbase_ru"), QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        QApplication::installTranslator(&qt);
    MaintenanceDialog dialog;
    dialog.adjustSize();
    return dialog.grab().save(QString::fromLocal8Bit(argv[1])) ? 0 : 1;
}
