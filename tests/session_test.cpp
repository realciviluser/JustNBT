#include "core/Config.h"
#include "core/NbtFile.h"
#include "ui/MainWindow.h"
#include "ui/NbtEditor.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QMenu>
#include <QMenuBar>
#include <QSettings>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTreeView>

#include <cstdio>

namespace {
int failures = 0;

void check(bool ok, const char* what)
{
    std::printf("%-58s %s\n", what, ok ? "ok" : "FAILED");
    if (!ok)
        ++failures;
}

bool writeTestLevelDat(const QString& path)
{
    justnbt::NbtFile file;
    file.format.endian = nbt::Endian::Big;
    file.format.compression = justnbt::Compression::Gzip;
    file.root = std::make_unique<nbt::Tag>(nbt::TagType::Compound);
    nbt::Tag* data = file.root->append(std::make_unique<nbt::Tag>(nbt::TagType::Compound, "Data"));
    auto name = std::make_unique<nbt::Tag>(nbt::TagType::String, "LevelName");
    name->string = "Test world";
    data->append(std::move(name));
    auto version = std::make_unique<nbt::Tag>(nbt::TagType::Int, "DataVersion");
    version->integer = 3953;
    data->append(std::move(version));

    const auto bytes = file.encode();
    QFile out(path);
    return out.open(QIODevice::WriteOnly)
        && out.write(reinterpret_cast<const char*>(bytes.data()), qint64(bytes.size())) == qint64(bytes.size());
}
}

int main(int argc, char** argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    QApplication::setOrganizationName(QStringLiteral("JustNBT_SessionTest"));
    QApplication::setApplicationName(QStringLiteral("JustNBT_SessionTest"));
    QApplication app(argc, argv);
    const QStringList args = app.arguments();
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    check(appData.contains(QLatin1String("JustNBT_SessionTest")), "test uses its own app data folder");

    check(QFile::exists(QStringLiteral(":/icon.ico")) && !QIcon(QStringLiteral(":/icon.ico")).isNull(),
          "the program icon is compiled into the binary");
    check(QIcon(QStringLiteral(":/icon.ico")).availableSizes().size() >= 6,
          "the icon carries all of its sizes (16 to 256)");

    QTemporaryDir temp;
    const QString present = temp.filePath(QStringLiteral("level.dat"));
    const QString missing = temp.filePath(QStringLiteral("gone.dat"));
    if (args.size() >= 2)
        check(QFile::copy(args[1], present), "copied a level.dat into the temp folder");
    else
        check(writeTestLevelDat(present), "wrote a level.dat of its own into the temp folder");

    QSettings& config = justnbt::settings();
    config.setValue(QStringLiteral("worlds/autoScan"), false);
    config.setValue(QStringLiteral("session/restoreTabs"), true);
    config.remove(QStringLiteral("session/tabs"));
    config.beginWriteArray(QStringLiteral("session/tabs"));
    config.setArrayIndex(0);
    config.setValue(QStringLiteral("kind"), QStringLiteral("file"));
    config.setValue(QStringLiteral("path"), present);
    config.setValue(QStringLiteral("title"), QStringLiteral("level.dat"));
    config.setArrayIndex(1);
    config.setValue(QStringLiteral("kind"), QStringLiteral("file"));
    config.setValue(QStringLiteral("path"), missing);
    config.setValue(QStringLiteral("title"), QStringLiteral("gone.dat"));
    config.setValue(QStringLiteral("active"), true);
    config.endArray();
    config.sync();

    {
        MainWindow window;
        window.setAttribute(Qt::WA_DontShowOnScreen, true);
        window.show();
        QApplication::processEvents();
        QApplication::processEvents();

        auto* tabs = window.findChild<QTabWidget*>();
        check(tabs != nullptr, "the window has a tab bar");
        check(tabs && tabs->count() == 1, "one tab restored, the missing file skipped");

        auto* editor = tabs && tabs->count() ? qobject_cast<NbtEditor*>(tabs->widget(0)) : nullptr;
        check(editor != nullptr, "the restored tab is an NBT editor");
        if (editor) {
            auto* view = editor->findChild<QTreeView*>();
            const int rootRows = view ? view->model()->rowCount(view->model()->index(0, 0)) : -1;
            check(rootRows > 0, "the tree shows the tags of the file");

            auto outside = justnbt::NbtFile::load(present);
            nbt::Tag* data = outside->root->child("Data");
            nbt::Tag* victim = data ? data->child("LevelName") : nullptr;
            const bool haveVictim = victim && victim->type == nbt::TagType::String;
            check(haveVictim, "the file has Data/LevelName to change");
            if (haveVictim) {
                victim->string = "changed by session_test";
                const auto bytes = outside->encode();
                QFile out(present);
                out.open(QIODevice::WriteOnly | QIODevice::Truncate);
                out.write(reinterpret_cast<const char*>(bytes.data()), qint64(bytes.size()));
                out.close();

                const nbt::Tag* before = editor->file().root->child("Data")->child("LevelName");
                check(before && before->string != "changed by session_test", "the open tab still shows the old name");
                editor->reload();
                const nbt::Tag* after = editor->file().root->child("Data")->child("LevelName");
                check(after && after->string == "changed by session_test", "after Обновить the tab shows the new name");
                check(!editor->isModified(), "a re-read tab is not marked as modified");
                check(editor->findChild<QTreeView*>()->model()->rowCount(QModelIndex()) == 1,
                      "the tree still has its single root row");
            }
        }

        QAction* languageMenu = nullptr;
        for (QAction* action : window.menuBar()->actions())
            if (QMenu* menu = action->menu())
                for (QAction* item : menu->actions())
                    if (item->menu() && item->text().contains(QLatin1String("anguage")))
                        languageMenu = item;
        check(languageMenu != nullptr, "the File menu offers a language choice");
        if (languageMenu) {
            const auto choices = languageMenu->menu()->actions();
            int checked = 0;
            for (QAction* item : choices)
                checked += item->isChecked() ? 1 : 0;
            check(choices.size() == 3, "three languages: automatic, English, Russian");
            check(checked == 1, "exactly one of them is ticked");
        }

        window.close();
    }

    justnbt::setConfiguredLanguage(QStringLiteral("ru"));
    check(justnbt::uiLanguage() == QLatin1String("ru"), "a chosen language is used");
    justnbt::setConfiguredLanguage(QStringLiteral("en"));
    check(justnbt::uiLanguage() == QLatin1String("en"), "and the other one as well");
    justnbt::setConfiguredLanguage(QStringLiteral("auto"));
    check(justnbt::configuredLanguage() == QLatin1String("auto"), "automatic can be chosen back");
    check(justnbt::uiLanguage() == QLatin1String("ru") || justnbt::uiLanguage() == QLatin1String("en"),
          "automatic gives one of the two languages");

    config.sync();
    const int savedCount = config.beginReadArray(QStringLiteral("session/tabs"));
    QString savedPath;
    if (savedCount > 0) {
        config.setArrayIndex(0);
        savedPath = config.value(QStringLiteral("path")).toString();
    }
    config.endArray();
    check(savedCount == 1, "closing wrote exactly the one open tab back to the config");
    check(savedPath == present, "and it is the file that was open");

    QDir(appData).removeRecursively();
    QDir().rmdir(QFileInfo(appData).absolutePath());
    std::printf("\n%d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
