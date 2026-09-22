#include "core/Worlds.h"
#include "ui/MapTab.h"
#include "ui/MapView.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTimer>

#include <cstdio>

int main(int argc, char** argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    QApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() < 5) {
        std::printf("usage: map_snapshot <world> <dimension> <zoom> <out.png> [w h] [grid]\n");
        return 2;
    }
    const auto world = justnbt::probeWorld(args[1]);
    if (!world) {
        std::printf("not a world: %s\n", qPrintable(args[1]));
        return 1;
    }

    MapTab tab(*world, args[2]);
    tab.resize(args.size() >= 7 ? args[5].toInt() : 1200, args.size() >= 7 ? args[6].toInt() : 800);
    tab.setAttribute(Qt::WA_DontShowOnScreen, true);
    tab.show();
    auto* view = tab.findChild<MapView*>();
    view->setZoom(args[3].toInt());
    if (args.size() >= 8 && args[7] == QLatin1String("grid"))
        view->setShowGrid(true);

    QElapsedTimer timer;
    timer.start();
    QTimer idle;
    idle.setSingleShot(true);
    QObject::connect(view, &MapView::progressChanged, [&](int ready, int total, int failed) {
        std::printf("\r%lld ms: %d/%d regions, failed chunks %d   ", qlonglong(timer.elapsed()), ready, total, failed);
        idle.start(1500);
    });
    QObject::connect(&idle, &QTimer::timeout, [&] {
        if (args.contains(QLatin1String("select"))) {
            QKeyEvent ctrl(QEvent::KeyPress, Qt::Key_Control, Qt::ControlModifier);
            QApplication::sendEvent(view, &ctrl);
            QKeyEvent ctrlUp(QEvent::KeyRelease, Qt::Key_Control, Qt::NoModifier);
            QApplication::sendEvent(view, &ctrlUp);
            std::printf("select mode after Ctrl: %s\n", view->selectMode() ? "on" : "OFF (wrong)");

            auto drag = [&](double x1, double y1, double x2, double y2, Qt::KeyboardModifiers mods) {
                const QPointF from(view->width() * x1, view->height() * y1);
                const QPointF to(view->width() * x2, view->height() * y2);
                QMouseEvent press(QEvent::MouseButtonPress, from, view->mapToGlobal(from), Qt::LeftButton,
                                  Qt::LeftButton, mods);
                QMouseEvent move(QEvent::MouseMove, to, view->mapToGlobal(to), Qt::NoButton, Qt::LeftButton, mods);
                QMouseEvent release(QEvent::MouseButtonRelease, to, view->mapToGlobal(to), Qt::LeftButton,
                                    Qt::NoButton, mods);
                for (QMouseEvent* e : {&press, &move, &release})
                    QApplication::sendEvent(view, e);
            };
            drag(0.2, 0.25, 0.45, 0.55, Qt::NoModifier);
            drag(0.55, 0.45, 0.8, 0.75, Qt::ShiftModifier);
            std::printf("selected areas: %lld\n", qlonglong(view->selections().size()));
        }
        std::printf("tab min %dx%d, tab size %dx%d, view %dx%d\n", tab.minimumSizeHint().width(),
                    tab.minimumSizeHint().height(), tab.width(), tab.height(), view->width(), view->height());
        tab.grab().save(args[4]);
        std::printf("\nsaved %s after %lld ms\n", qPrintable(args[4]), qlonglong(timer.elapsed()));
        app.quit();
    });
    QObject::connect(view, &MapView::tilesListed, [&] { tab.grab(); });
    tab.grab();
    idle.start(3000);
    return app.exec();
}
