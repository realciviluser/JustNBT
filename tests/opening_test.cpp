#include "core/Config.h"
#include "core/NbtFile.h"
#include "ui/FileAssociation.h"
#include "ui/InventoryPanel.h"
#include "ui/MapTab.h"
#include "ui/MapView.h"
#include "ui/MainWindow.h"
#include "ui/NbtEditor.h"
#include "ui/SingleInstance.h"

#include <QApplication>
#include <QDir>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QMimeData>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QTreeView>
#include <QTreeWidget>
#include <QUrl>

#include <cstdio>

namespace {
int failures = 0;

void check(bool ok, const QString& what)
{
    std::printf("%-64s %s\n", qPrintable(what), ok ? "ok" : "FAILED");
    if (!ok)
        ++failures;
}

bool writeLevelDat(const QString& path, const char* levelName)
{
    justnbt::NbtFile file;
    file.format.endian = nbt::Endian::Big;
    file.format.compression = justnbt::Compression::Gzip;
    file.root = std::make_unique<nbt::Tag>(nbt::TagType::Compound);
    nbt::Tag* data = file.root->append(std::make_unique<nbt::Tag>(nbt::TagType::Compound, "Data"));
    auto name = std::make_unique<nbt::Tag>(nbt::TagType::String, "LevelName");
    name->string = levelName;
    data->append(std::move(name));
    const auto bytes = file.encode();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile out(path);
    return out.open(QIODevice::WriteOnly)
        && out.write(reinterpret_cast<const char*>(bytes.data()), qint64(bytes.size())) == qint64(bytes.size());
}

template <class F>
bool waitFor(F done)
{
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < 3000)
        QApplication::processEvents(QEventLoop::AllEvents, 20);
    return done();
}

QStringList tabIdentities(QTabWidget* tabs)
{
    QStringList ids;
    for (int i = 0; i < tabs->count(); ++i)
        ids << QDir::fromNativeSeparators(tabs->widget(i)->property("identity").toString());
    return ids;
}

bool drop(QWidget* target, const QStringList& paths)
{
    QMimeData data;
    QList<QUrl> urls;
    for (const QString& p : paths)
        urls << QUrl::fromLocalFile(p);
    data.setUrls(urls);
    const QPoint where = target->rect().center();
    QDragEnterEvent enter(where, Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(target, &enter);
    QDropEvent dropEvent(QPointF(where), Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(target, &dropEvent);
    return enter.isAccepted() && dropEvent.isAccepted();
}

bool handOverFromAnotherInstance(const QString& key, const QStringList& paths)
{
    bool handedOver = false;
    QThread* thread = QThread::create([&] {
        SingleInstance later(key);
        handedOver = later.handOver(paths);
    });
    thread->start();
    waitFor([&] { return thread->isFinished(); });
    thread->wait();
    delete thread;
    return handedOver;
}

QString* watchForMessageBox()
{
    auto* text = new QString;
    auto* poll = new QTimer;
    QObject::connect(poll, &QTimer::timeout, [poll, text] {
        if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
            *text = box->text();
            box->done(QMessageBox::Ok);
            poll->deleteLater();
        }
    });
    poll->start(30);
    return text;
}
}

bool writePlayer(const QString& path)
{
    justnbt::NbtFile file;
    file.format.endian = nbt::Endian::Big;
    file.format.compression = justnbt::Compression::Gzip;
    file.root = std::make_unique<nbt::Tag>(nbt::TagType::Compound);
    auto pos = std::make_unique<nbt::Tag>(nbt::TagType::List, "Pos");
    pos->listType = nbt::TagType::Double;
    for (double v : {100.7, 64.0, -49.2})
        pos->append(std::make_unique<nbt::Tag>(nbt::TagType::Double))->floating = v;
    file.root->append(std::move(pos));
    file.root->append(std::make_unique<nbt::Tag>(nbt::TagType::String, "Dimension"))->string = "minecraft:overworld";
    file.root->append(std::make_unique<nbt::Tag>(nbt::TagType::List, "Inventory"))->listType = nbt::TagType::Compound;
    file.root->append(std::make_unique<nbt::Tag>(nbt::TagType::Compound, "abilities"));
    const auto bytes = file.encode();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile out(path);
    return out.open(QIODevice::WriteOnly)
        && out.write(reinterpret_cast<const char*>(bytes.data()), qint64(bytes.size())) == qint64(bytes.size());
}

int main(int argc, char** argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    QApplication::setOrganizationName(QStringLiteral("JustNBT_OpeningTest"));
    QApplication::setApplicationName(QStringLiteral("JustNBT_OpeningTest"));
    QApplication app(argc, argv);
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);

    QTemporaryDir temp;
    const QString fileA = temp.filePath(QStringLiteral("a/level.dat"));
    const QString fileB = temp.filePath(QStringLiteral("b/player.dat"));
    const QString worldDir = temp.filePath(QStringLiteral("My world"));
    const QString notNbt = temp.filePath(QStringLiteral("notes.txt"));
    check(writeLevelDat(fileA, "A") && writeLevelDat(fileB, "B") && writeLevelDat(worldDir + "/level.dat", "Dropped"),
          QStringLiteral("test files written"));
    {
        QFile txt(notNbt);
        txt.open(QIODevice::WriteOnly);
        txt.write("this is not NBT at all");
    }

    QSettings& config = justnbt::settings();
    config.setValue(QStringLiteral("worlds/autoScan"), false);
    config.setValue(QStringLiteral("session/restoreTabs"), false);

    {
        MainWindow window;
        window.setAttribute(Qt::WA_DontShowOnScreen, true);
        window.show();
        QApplication::processEvents();
        auto* tabs = window.findChild<QTabWidget*>();
        auto* worlds = window.findChild<QTreeWidget*>(QStringLiteral("worlds"));

        check(window.acceptDrops(), QStringLiteral("the window takes drops"));
        check(drop(&window, {fileA}), QStringLiteral("a file dropped on the window is taken"));
        check(waitFor([&] { return tabs->count() == 1; }), QStringLiteral("and opens as a tab"));
        check(tabIdentities(tabs).value(0) == QFileInfo(fileA).absoluteFilePath(),
              QStringLiteral("the tab shows that file"));

        auto* tree = tabs->widget(0)->findChild<QTreeView*>();
        check(tree && drop(tree->viewport(), {fileB}), QStringLiteral("a file dropped on a tag tree reaches the window"));
        check(waitFor([&] { return tabs->count() == 2; }), QStringLiteral("and opens as a second tab"));

        check(drop(&window, {fileA}) && waitFor([&] { return tabs->currentIndex() == 0; }),
              QStringLiteral("dropping an open file again just shows its tab"));
        check(tabs->count() == 2, QStringLiteral("without a second copy of it"));

        check(drop(&window, {worldDir}), QStringLiteral("a world folder dropped on the window is taken"));
        const bool listed = waitFor([&] {
            for (QTreeWidgetItemIterator it(worlds); *it; ++it)
                if ((*it)->text(0) == QLatin1String("Dropped"))
                    return true;
            return false;
        });
        check(listed, QStringLiteral("and appears in the world list"));
        check(worlds->currentItem() && worlds->currentItem()->text(0) == QLatin1String("Dropped"),
              QStringLiteral("selected"));
        check(config.value(QStringLiteral("worlds/manual")).toStringList().contains(QFileInfo(worldDir).absoluteFilePath()),
              QStringLiteral("and remembered like one opened by hand"));

        const QString fileC = temp.filePath(QStringLiteral("c/level.dat"));
        writeLevelDat(fileC, "C");
        QString* message = watchForMessageBox();
        window.openPaths({fileC, notNbt});
        waitFor([&] { return !message->isEmpty(); });
        check(tabs->count() == 3, QStringLiteral("of several files the good one opens"));
        check(message->contains(QLatin1String("notes.txt")), QStringLiteral("and one message names the bad one"));
        std::printf("      message: %s\n", qPrintable(message->simplified()));
        delete message;

        QMimeData web;
        web.setUrls({QUrl(QStringLiteral("https://example.com/level.dat"))});
        QDragEnterEvent enter(QPoint(5, 5), Qt::CopyAction, &web, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&window, &enter);
        check(!enter.isAccepted(), QStringLiteral("a web link is not taken as a file"));

        SingleInstance first(QStringLiteral("opening_test"));
        check(!first.handOver({}), QStringLiteral("the first instance keeps running"));
        QStringList received;
        bool gotSomething = false;
        QObject::connect(&first, &SingleInstance::pathsReceived, [&](const QStringList& paths) {
            received = paths;
            gotSomething = true;
            window.openPaths(paths);
        });
        const QString fileD = temp.filePath(QStringLiteral("d/level.dat"));
        writeLevelDat(fileD, "D");
        check(handOverFromAnotherInstance(QStringLiteral("opening_test"), {fileD}),
              QStringLiteral("a second instance hands its file over"));
        check(waitFor([&] { return gotSomething; }), QStringLiteral("the first one receives it"));
        check(received == QStringList({fileD}), QStringLiteral("exactly that file"));
        check(waitFor([&] { return tabs->count() == 4; }), QStringLiteral("and it opens as a tab of the running window"));

        gotSomething = false;
        check(handOverFromAnotherInstance(QStringLiteral("opening_test"), {}),
              QStringLiteral("a start without files is handed over too"));
        check(waitFor([&] { return gotSomething; }) && received.isEmpty(),
              QStringLiteral("and only asks the window to come up"));

        SingleInstance otherUser(QStringLiteral("someone else"));
        check(!otherUser.handOver({fileA}), QStringLiteral("another key (user) is a separate instance"));
        check(!received.contains(fileA), QStringLiteral("and its files do not end up here"));

        const QString playerFile = worldDir + QStringLiteral("/playerdata/player.dat");
        check(writePlayer(playerFile), QStringLiteral("a player file inside the world"));
        const int before = tabs->count();
        window.openPaths({playerFile});
        check(waitFor([&] { return tabs->count() == before + 1; }), QStringLiteral("opens as a tab"));
        auto* editor = qobject_cast<NbtEditor*>(tabs->currentWidget());
        InventoryPanel* panel = editor ? editor->inventoryPanel() : nullptr;
        check(panel && panel->holder(), QStringLiteral("its inventory panel shows the player"));
        if (panel) {
            emit panel->showOnMap(QStringLiteral("minecraft:overworld"), 100, -50);
            auto* map = qobject_cast<MapTab*>(tabs->currentWidget());
            MapView* view = map ? map->findChild<MapView*>() : nullptr;
            check(map != nullptr, QStringLiteral("\"On the map\" opens the map of that world"));
            check(view && view->pin() == QPoint(100, -50), QStringLiteral("  with a pin where the player stands"));
        }

        window.close();
    }

    const QString exe = QStringLiteral("C:/Tools/JustNBT/JustNBT.exe");
    const justnbt::AssociationPlan plan = justnbt::associationPlan(exe);
    const QString command = QLatin1Char('"') + QDir::toNativeSeparators(exe) + QStringLiteral("\" \"%1\"");
    bool commandOk = false, noDefaultTaken = true, everyExtension = true;
    for (const justnbt::RegistryEntry& e : plan.entries + plan.sharedValues) {
        if (e.key.endsWith(QLatin1String("/shell/open/command")) && e.value == command)
            commandOk = true;
        if (e.key.startsWith(QLatin1Char('.')) && !e.key.contains(QLatin1Char('/')))
            noDefaultTaken = false;
        if (e.key.startsWith(QLatin1Char('.')) && e.name == QLatin1String("Default"))
            noDefaultTaken = false;
    }
    for (const QString& ext : justnbt::nbtFileExtensions()) {
        bool found = false;
        for (const justnbt::RegistryEntry& e : plan.sharedValues)
            found = found || (e.key == QLatin1Char('.') + ext + QStringLiteral("/OpenWithProgids")
                              && e.name == QLatin1String("JustNBT.NBTFile"));
        everyExtension = everyExtension && found;
    }
    check(commandOk, QStringLiteral("Open with: starts this exe with the file as its argument"));
    check(everyExtension, QStringLiteral("Open with: offered for every NBT extension"));
    check(noDefaultTaken, QStringLiteral("Open with: the default program of .dat is never taken"));
    check(plan.ownKeys == QStringList({QStringLiteral("JustNBT.NBTFile"), QStringLiteral("Applications/JustNBT.exe")}),
          QStringLiteral("Open with: switching off removes only JustNBT's own keys"));
    check(justnbt::nbtFileExtensions().contains(QLatin1String("mcstructure"))
              && justnbt::nbtFileExtensions().contains(QLatin1String("litematic")),
          QStringLiteral("schematics and Bedrock structures count as NBT files"));

    QDir(appData).removeRecursively();
    QDir().rmdir(QFileInfo(appData).absolutePath());
    std::printf("\n%d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
