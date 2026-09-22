#include "ui/MapView.h"

#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QSignalSpy>

#include <cstdio>

namespace {
int failures = 0;

void check(bool ok, const QString& what)
{
    std::printf("%-62s %s\n", qPrintable(what), ok ? "ok" : "FAILED");
    if (!ok)
        ++failures;
}

QPointF chunkPoint(const MapView& view, int cx, int cz)
{
    return {view.width() / 2.0 + cx * 16 + 8, view.height() / 2.0 + cz * 16 + 8};
}

void send(MapView& view, QEvent::Type type, QPointF at, Qt::MouseButton button, Qt::MouseButtons held,
          Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    QMouseEvent e(type, at, view.mapToGlobal(at), button, held, mods);
    QApplication::sendEvent(&view, &e);
}

void drag(MapView& view, QPointF from, QPointF to, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    send(view, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton, mods);
    send(view, QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton, mods);
    send(view, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton, mods);
}

void click(MapView& view, QPointF at)
{
    send(view, QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton);
    send(view, QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton);
}

Qt::CursorShape cursorAt(MapView& view, QPointF at)
{
    send(view, QEvent::MouseMove, at, Qt::NoButton, Qt::NoButton);
    return view.cursor().shape();
}

void key(MapView& view, int k)
{
    QKeyEvent e(QEvent::KeyPress, k, Qt::NoModifier);
    QApplication::sendEvent(&view, &e);
}
}

int main(int argc, char** argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    QApplication app(argc, argv);

    MapView view;
    view.resize(800, 600);
    view.setZoom(0);
    view.centerOn(0, 0);
    view.setSelectMode(true);
    QSignalSpy changes(&view, &MapView::selectionChanged);

    drag(view, chunkPoint(view, 0, 0), chunkPoint(view, 3, 2));
    check(view.selections() == QList<QRect>({QRect(QPoint(0, 0), QPoint(3, 2))}), QStringLiteral("drawing makes a rectangle"));
    drag(view, chunkPoint(view, -6, -6), chunkPoint(view, -4, -4), Qt::ShiftModifier);
    check(view.selections().size() == 2, QStringLiteral("Shift adds a second one"));
    check(view.currentSelection() == 1, QStringLiteral("the rectangle just drawn is the current one"));
    const QRect b = view.selections()[1];

    const QPointF insideA = chunkPoint(view, 1, 1);
    check(cursorAt(view, insideA) == Qt::SizeAllCursor, QStringLiteral("inside a rectangle: four arrows (move)"));
    check(cursorAt(view, QPointF(view.width() / 2.0, view.height() / 2.0 + 24)) == Qt::SizeHorCursor,
          QStringLiteral("on its left edge: resize sideways"));
    check(cursorAt(view, QPointF(view.width() / 2.0 + 64, view.height() / 2.0 + 48)) == Qt::SizeFDiagCursor,
          QStringLiteral("on its corner: resize diagonally"));
    check(cursorAt(view, chunkPoint(view, 20, 20)) == Qt::CrossCursor, QStringLiteral("outside: cross (new rectangle)"));

    const int before = int(changes.count());
    drag(view, insideA, chunkPoint(view, 6, 4));
    check(view.selections()[0] == QRect(QPoint(5, 3), QPoint(8, 5)), QStringLiteral("dragging the inside moves it, size kept"));
    check(view.selections()[1] == b, QStringLiteral("the other rectangle does not move"));
    check(view.selections().size() == 2, QStringLiteral("moving does not start a new rectangle"));
    check(view.currentSelection() == 0, QStringLiteral("the moved one becomes the current one"));
    check(changes.count() > before, QStringLiteral("the map tab hears about the move"));

    click(view, chunkPoint(view, -5, -5));
    check(view.currentSelection() == 1 && view.selections()[1] == b, QStringLiteral("a click selects without moving"));

    key(view, Qt::Key_Delete);
    check(view.selections() == QList<QRect>({QRect(QPoint(5, 3), QPoint(8, 5))}),
          QStringLiteral("Delete removes only the current rectangle"));
    check(view.currentSelection() == -1, QStringLiteral("after that none is current"));
    key(view, Qt::Key_Delete);
    check(view.selections().size() == 1, QStringLiteral("Delete with none current removes nothing"));

    drag(view, chunkPoint(view, 6, 4), chunkPoint(view, 7, 7), Qt::ShiftModifier);
    check(view.selections().size() == 2 && view.selections()[0] == QRect(QPoint(5, 3), QPoint(8, 5)),
          QStringLiteral("Shift inside a rectangle draws a new one"));

    const QRectF aOnScreen(chunkPoint(view, 5, 3) - QPointF(8, 8), chunkPoint(view, 8, 5) + QPointF(8, 8));
    drag(view, QPointF(aOnScreen.right(), aOnScreen.center().y()), chunkPoint(view, 10, 4));
    check(view.selections()[0] == QRect(QPoint(5, 3), QPoint(10, 5)), QStringLiteral("dragging the right edge resizes"));

    key(view, Qt::Key_Escape);
    check(view.selections().isEmpty(), QStringLiteral("Esc clears them all"));

    drag(view, chunkPoint(view, 0, 0), chunkPoint(view, 1, 1));
    view.setSelectMode(false);
    check(view.selections().isEmpty() && view.currentSelection() == -1,
          QStringLiteral("leaving the selection mode clears them"));

    std::printf("\n%d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
