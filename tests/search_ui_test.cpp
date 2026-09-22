#include "TestWorld.h"

#include "core/WorldSearch.h"
#include "core/Worlds.h"
#include "ui/MapTab.h"
#include "ui/MapView.h"
#include "ui/SearchPanel.h"

#include <QApplication>
#include <QCheckBox>
#include <QKeyEvent>
#include <QClipboard>
#include <QElapsedTimer>
#include <QMenu>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTableView>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTranslator>

#include <cstdio>

using justnbt::SearchKind;

namespace {
int failures = 0;

void check(bool ok, const QString& what)
{
    std::printf("%-64s %s\n", qPrintable(what), ok ? "ok" : "FAILED");
    if (!ok)
        ++failures;
}

template <class F>
bool waitFor(F done, int ms = 10000)
{
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < ms)
        QApplication::processEvents(QEventLoop::AllEvents, 20);
    return done();
}

bool runSearch(SearchPanel* panel, SearchKind kind, const QString& text, bool selectedOnly = false)
{
    QSignalSpy finished(panel, &SearchPanel::searchFinished);
    panel->search(kind, text, selectedOnly);
    return waitFor([&] { return finished.count() > 0; });
}
}

int main(int argc, char** argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    QApplication::setOrganizationName(QStringLiteral("JustNBT_SearchUiTest"));
    QApplication::setApplicationName(QStringLiteral("JustNBT_SearchUiTest"));
    QApplication app(argc, argv);
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QTranslator translator;
    if (argc >= 3 && translator.load(QString::fromLocal8Bit(argv[2])))
        QApplication::installTranslator(&translator);

    QTemporaryDir temp;
    const QString dir = temp.filePath(QStringLiteral("world"));
    check(testworld::buildWorld(dir), QStringLiteral("test world built"));
    const auto world = justnbt::probeWorld(dir);
    if (!world) {
        std::printf("\n%d failures\n", failures + 1);
        return 1;
    }

    MapTab tab(*world, world->dimensions.first().id);
    tab.resize(1300, 760);
    tab.setAttribute(Qt::WA_DontShowOnScreen, true);
    tab.show();
    QApplication::processEvents();
    auto* view = tab.findChild<MapView*>();
    SearchPanel* panel = tab.searchPanel();
    auto* table = panel->findChild<QTableView*>();

    check(!panel->isVisible(), QStringLiteral("the panel starts hidden"));
    tab.showSearch(true);
    check(panel->isVisible(), QStringLiteral("the Search button (Ctrl+F) opens it"));

    check(runSearch(panel, SearchKind::Block, QStringLiteral("diamond_ore")), QStringLiteral("a block search finishes"));
    check(panel->hits().size() == 4 && table->model()->rowCount() == 4, QStringLiteral("the list shows the 4 ores"));
    check(view->searchMarkerCount() == 4, QStringLiteral("the map shows them as 4 dots"));

    QSignalSpy current(panel, &SearchPanel::currentHitChanged);
    table->setCurrentIndex(table->model()->index(2, 0));
    check(current.count() == 1, QStringLiteral("choosing a find in the list goes to it on the map"));

    QSignalSpy opened(&tab, &MapTab::openChunk);
    const QModelIndex first = table->model()->index(0, 0);
    const QModelIndex position = table->model()->index(0, 1);
    const int x = position.data(Qt::UserRole + 3).toInt(), y = position.data(Qt::UserRole + 4).toInt(),
              z = position.data(Qt::UserRole + 5).toInt();
    emit table->doubleClicked(first);
    check(opened.count() == 1, QStringLiteral("a double click opens the chunk of the find"));
    if (opened.count() == 1) {
        const auto args = opened.takeFirst();
        check(args.at(1).toInt() == (x >> 4) && args.at(2).toInt() == (z >> 4) && args.at(3).toString() == "region",
              QStringLiteral("  the right chunk, from the region files"));
    }

    const bool pictureRun = argc >= 2;
    if (!pictureRun) {
        QTimer::singleShot(200, [] {
            if (auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget())) {
                menu->actions().last()->trigger();
                menu->close();
            }
        });
        QApplication::clipboard()->clear();
        const QRect cell = table->visualRect(table->model()->index(0, 0));
        emit table->customContextMenuRequested(cell.center());
        const QString copied = QApplication::clipboard()->text();
        check(copied == QStringLiteral("%1 %2 %3").arg(x).arg(y).arg(z),
              QStringLiteral("Copy the coordinates: \"x y z\" for /tp"));
    }

    check(runSearch(panel, SearchKind::Block, QStringLiteral("spawner")) && panel->hits().size() == 1,
          QStringLiteral("a spawner is found"));
    const QString spawnerDetails = table->model()->index(0, 2).data().toString();
    check(spawnerDetails.contains(QLatin1String("Zombie"), Qt::CaseInsensitive)
              || spawnerDetails.contains(QStringLiteral("Зомби")),
          QStringLiteral("  and the list says which mob it makes"));
    std::printf("      details: %s\n", qPrintable(spawnerDetails));

    check(runSearch(panel, SearchKind::Item, QStringLiteral("minecraft:diamond")) && panel->hits().size() == 2,
          QStringLiteral("items: the chest and the dropped diamond"));
    bool eight = false;
    for (int r = 0; r < table->model()->rowCount(); ++r)
        eight = eight || table->model()->index(r, 2).data().toString().contains(QStringLiteral("× 8"));
    check(eight, QStringLiteral("  the chest holds 8 of them"));

    check(runSearch(panel, SearchKind::Entity, QStringLiteral("villager")) && panel->hits().size() == 1,
          QStringLiteral("entities: the villager"));
    check(table->model()->index(0, 2).data().toString().contains(QStringLiteral("Bob")),
          QStringLiteral("  with his name in the details"));

    check(runSearch(panel, SearchKind::Poi, QStringLiteral("home")) && panel->hits().size() == 1,
          QStringLiteral("points of interest: the bed"));
    const size_t before = panel->hits().size();
    panel->search(SearchKind::Block, QString());
    check(!panel->isSearching() && panel->hits().size() == before, QStringLiteral("an empty field starts nothing"));

    view->setSelectMode(true);
    const QPointF c00(view->width() / 2.0, view->height() / 2.0);
    view->centerOn(8, 8);
    QMouseEvent press(QEvent::MouseButtonPress, c00, view->mapToGlobal(c00), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, c00, view->mapToGlobal(c00), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(view, &press);
    QApplication::sendEvent(view, &release);
    check(view->selections().size() == 1 && view->selections()[0] == QRect(0, 0, 1, 1),
          QStringLiteral("chunk 0,0 selected on the map"));
    check(runSearch(panel, SearchKind::Block, QStringLiteral("diamond_ore"), true) && panel->hits().size() == 2,
          QStringLiteral("only in the selected area: its 2 ores"));
    view->setSelectMode(false);

    runSearch(panel, SearchKind::Item, QStringLiteral("diamond"));
    table->setCurrentIndex(table->model()->index(0, 0));
    view->setZoom(2);
    if (argc >= 2)
        check(tab.grab().save(QString::fromLocal8Bit(argv[1])), QStringLiteral("picture saved"));

    view->setSelectMode(true);
    tab.showPlace(8, 8);
    {
        const QPointF centre(view->width() / 2.0, view->height() / 2.0);
        QMouseEvent down(QEvent::MouseButtonPress, centre, view->mapToGlobal(centre), Qt::LeftButton, Qt::LeftButton,
                         Qt::NoModifier);
        QMouseEvent up(QEvent::MouseButtonRelease, centre, view->mapToGlobal(centre), Qt::LeftButton, Qt::NoButton,
                       Qt::NoModifier);
        QApplication::sendEvent(view, &down);
        QApplication::sendEvent(view, &up);
    }
    std::printf("      pin %d, selections %d, dots %d\n", int(view->pin().has_value()), int(view->selections().size()),
                view->searchMarkerCount());
    check(view->pin().has_value() && !view->selections().isEmpty() && view->searchMarkerCount() > 0,
          QStringLiteral("a pin, a selection and found dots on the map"));
    tab.clearMarks();
    check(!view->pin() && view->selections().isEmpty() && view->searchMarkerCount() == 0 && panel->hits().empty()
              && table->model()->rowCount() == 0,
          QStringLiteral("\"Clear marks\" takes them all away"));
    view->setSelectMode(false);
    runSearch(panel, SearchKind::Item, QStringLiteral("diamond"));

    if (world->dimensions.size() > 1) {
        tab.showDimension(world->dimensions[1].id);
        QApplication::processEvents();
        check(panel->hits().empty() && view->searchMarkerCount() == 0,
              QStringLiteral("another dimension drops the finds of this one"));
    }

    QCheckBox* grid = nullptr;
    for (auto* box : tab.findChildren<QCheckBox*>())
        if (box->toolTip().contains(QLatin1String("(Tab")) || box->toolTip().contains(QLatin1String("Tab ")))
            grid = box;
    if (grid) {
        const bool was = grid->isChecked();
        view->setFocus();
        QKeyEvent tabKey(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
        QApplication::sendEvent(view, &tabKey);
        check(grid->isChecked() != was, QStringLiteral("Tab on the map switches the grid"));
        for (auto* a : tab.actions())
            if (a->shortcut() == QKeySequence(Qt::Key_G))
                a->trigger();
        check(grid->isChecked() == was, QStringLiteral("and G switches it back"));
    } else {
        check(false, QStringLiteral("the grid box is there"));
    }

    tab.showSearch(false);
    check(!panel->isVisible(), QStringLiteral("the panel closes again"));

    QDir(appData).removeRecursively();
    QDir().rmdir(QFileInfo(appData).absolutePath());
    std::printf("\n%d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
