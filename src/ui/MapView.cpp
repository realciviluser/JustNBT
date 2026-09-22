#include "ui/MapView.h"

#include <QDir>
#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QRunnable>
#include <QWheelEvent>

#include <climits>
#include <cmath>

namespace {
constexpr int kTile = justnbt::RenderedTile::kSize;

quint64 packKey(int rx, int rz)
{
    return (quint64(quint32(rx)) << 32) | quint32(rz);
}

int keyX(quint64 key) { return int(qint32(key >> 32)); }
int keyZ(quint64 key) { return int(qint32(key & 0xFFFFFFFFu)); }

int floorDiv(double v, int d)
{
    return int(std::floor(v / d));
}
}

MapView::MapView(QWidget* parent) : QWidget(parent)
{
    pool_.setMaxThreadCount(std::max(1, QThread::idealThreadCount() - 1));
    full_.setMaxCost(64);
    mid_.setMaxCost(512);
    low_.setMaxCost(8192);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setCursor(Qt::OpenHandCursor);
}

MapView::~MapView()
{
    ++generation_;
    pool_.clear();
    pool_.waitForDone();
}

void MapView::setSource(std::shared_ptr<justnbt::TileSource> source, const justnbt::RenderOptions& options)
{
    ++generation_;
    pool_.clear();
    regions_.clear();
    full_.clear();
    mid_.clear();
    low_.clear();
    ready_ = 0;
    failedChunks_ = 0;
    options_ = options;
    source_ = std::move(source);
    listing_ = source_ != nullptr;
    emitProgress();
    update();
    if (!source_)
        return;

    const int generation = generation_;
    const auto src = source_;
    QPointer<MapView> self(this);
    pool_.start(QRunnable::create([self, src, generation] {
        auto tiles = src->listTiles();
        QMetaObject::invokeMethod(
            self.data(),
            [self, generation, tiles = std::move(tiles)] {
                if (self)
                    self->tilesArrived(generation, tiles);
            },
            Qt::QueuedConnection);
    }),
                INT_MAX);
}

void MapView::tilesArrived(int generation, const std::vector<justnbt::TilePos>& tiles)
{
    if (generation != generation_)
        return;
    listing_ = false;
    for (const justnbt::TilePos& t : tiles)
        regions_.insert(packKey(t.x, t.z), Region{});
    emitProgress();
    emit tilesListed();
    update();
}

void MapView::setRenderOptions(const justnbt::RenderOptions& options)
{
    options_ = options;
    reload();
}

void MapView::reload()
{
    ++generation_;
    pool_.clear();
    full_.clear();
    mid_.clear();
    low_.clear();
    for (Region& r : regions_) {
        r.pending = false;
        r.rendered = false;
        r.failedChunks = 0;
    }
    ready_ = 0;
    failedChunks_ = 0;
    emitProgress();
    update();
}

void MapView::centerOn(double blockX, double blockZ)
{
    centerX_ = blockX;
    centerZ_ = blockZ;
    update();
}

void MapView::setZoom(int level)
{
    level = std::clamp(level, kMinZoom, kMaxZoom);
    if (level == zoom_)
        return;
    zoom_ = level;
    emit zoomChanged(zoom_);
    update();
}

void MapView::setShowGrid(bool show)
{
    showGrid_ = show;
    update();
}

void MapView::setMarker(std::optional<QPointF> blockPos)
{
    marker_ = blockPos;
    update();
}

void MapView::setPin(std::optional<QPoint> block)
{
    pin_ = block;
    update();
}

void MapView::setBorder(std::optional<QRectF> border)
{
    border_ = border;
    update();
}

void MapView::setShowBorder(bool show)
{
    showBorder_ = show;
    update();
}

void MapView::setSelectMode(bool on)
{
    if (selectMode_ == on)
        return;
    selectMode_ = on;
    setCursor(on ? Qt::CrossCursor : Qt::OpenHandCursor);
    if (!on)
        clearSelection();
    emit selectModeChanged(on);
    update();
}

void MapView::clearSelection()
{
    selecting_ = false;
    resizing_ = {};
    moving_ = -1;
    current_ = -1;
    selections_.clear();
    emit selectionChanged(selections_);
    update();
}

MapView::Handle MapView::handleAt(const QPoint& pos) const
{
    constexpr double kGrab = 6.0;
    for (int i = selections_.size() - 1; i >= 0; --i) {
        const QRect& area = selections_[i];
        const QRectF r(toScreen(area.left() * 16.0, area.top() * 16.0),
                       toScreen((area.right() + 1) * 16.0, (area.bottom() + 1) * 16.0));
        if (!r.adjusted(-kGrab, -kGrab, kGrab, kGrab).contains(pos))
            continue;
        Handle h;
        h.left = std::abs(pos.x() - r.left()) <= kGrab;
        h.right = std::abs(pos.x() - r.right()) <= kGrab;
        h.top = std::abs(pos.y() - r.top()) <= kGrab;
        h.bottom = std::abs(pos.y() - r.bottom()) <= kGrab;
        if (h.left || h.right || h.top || h.bottom) {
            h.index = i;
            return h;
        }
    }
    return {};
}

int MapView::rectangleAt(const QPoint& pos) const
{
    for (int i = int(selections_.size()) - 1; i >= 0; --i) {
        const QRect& area = selections_[i];
        const QRectF r(toScreen(area.left() * 16.0, area.top() * 16.0),
                       toScreen((area.right() + 1) * 16.0, (area.bottom() + 1) * 16.0));
        if (r.contains(pos))
            return i;
    }
    return -1;
}

void MapView::updateSelectCursor(const QPoint& pos)
{
    const Handle handle = handleAt(pos);
    if (handle.valid()) {
        if ((handle.left && handle.top) || (handle.right && handle.bottom))
            setCursor(Qt::SizeFDiagCursor);
        else if ((handle.right && handle.top) || (handle.left && handle.bottom))
            setCursor(Qt::SizeBDiagCursor);
        else if (handle.left || handle.right)
            setCursor(Qt::SizeHorCursor);
        else
            setCursor(Qt::SizeVerCursor);
    } else if (rectangleAt(pos) >= 0) {
        setCursor(Qt::SizeAllCursor);
    } else {
        setCursor(Qt::CrossCursor);
    }
}

QPoint MapView::chunkAt(const QPointF& screen) const
{
    const QPointF b = toBlock(screen);
    return {floorDiv(std::floor(b.x()), 16), floorDiv(std::floor(b.y()), 16)};
}

bool MapView::focusNextPrevChild(bool)
{
    emit gridToggleRequested();
    return true;
}

void MapView::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Control) {
        if (!event->isAutoRepeat())
            ctrlAlone_ = true;
        return;
    }
    ctrlAlone_ = false;
    if (event->key() == Qt::Key_Escape && selectMode_) {
        clearSelection();
        return;
    }
    if (event->key() == Qt::Key_Delete && selectMode_ && current_ >= 0 && current_ < selections_.size()) {
        selections_.removeAt(current_);
        current_ = -1;
        emit selectionChanged(selections_);
        update();
        return;
    }
    QWidget::keyPressEvent(event);
}

void MapView::keyReleaseEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Control && !event->isAutoRepeat()) {
        if (ctrlAlone_)
            setSelectMode(!selectMode_);
        ctrlAlone_ = false;
        return;
    }
    QWidget::keyReleaseEvent(event);
}

void MapView::setSearchMarkers(std::vector<QPoint> blocks)
{
    searchMarkers_ = std::move(blocks);
    currentSearchMarker_ = -1;
    update();
}

void MapView::setCurrentSearchMarker(int index)
{
    currentSearchMarker_ = index >= 0 && index < int(searchMarkers_.size()) ? index : -1;
    update();
}

QPointF MapView::regionsCenter() const
{
    if (regions_.isEmpty())
        return {};
    std::vector<int> xs, zs;
    for (auto it = regions_.cbegin(); it != regions_.cend(); ++it) {
        xs.push_back(keyX(it.key()));
        zs.push_back(keyZ(it.key()));
    }
    std::nth_element(xs.begin(), xs.begin() + ptrdiff_t(xs.size() / 2), xs.end());
    std::nth_element(zs.begin(), zs.begin() + ptrdiff_t(zs.size() / 2), zs.end());
    return {xs[xs.size() / 2] * double(kTile) + kTile / 2.0, zs[zs.size() / 2] * double(kTile) + kTile / 2.0};
}

double MapView::scale() const
{
    return std::ldexp(1.0, zoom_);
}

QPointF MapView::toScreen(double bx, double bz) const
{
    return {(bx - centerX_) * scale() + width() / 2.0, (bz - centerZ_) * scale() + height() / 2.0};
}

QPointF MapView::toBlock(const QPointF& p) const
{
    return {(p.x() - width() / 2.0) / scale() + centerX_, (p.y() - height() / 2.0) / scale() + centerZ_};
}

void MapView::request(quint64 key, int priority)
{
    Region& r = regions_[key];
    if (r.pending)
        return;
    r.pending = true;

    const auto source = source_;
    const justnbt::TilePos pos{keyX(key), keyZ(key)};
    const justnbt::RenderOptions options = options_;
    const int generation = generation_;
    QPointer<MapView> self(this);
    pool_.start(QRunnable::create([self, source, pos, options, generation, key] {
        auto tile = source->tile(pos, options);
        QImage full(reinterpret_cast<const uchar*>(tile->pixels.data()), kTile, kTile, QImage::Format_ARGB32);
        full = full.copy();
        tile->pixels = {};
        QImage mid = full.scaled(128, 128, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        QImage low = mid.scaled(32, 32, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        QMetaObject::invokeMethod(
            self.data(),
            [self, generation, key, tile, full, mid, low] {
                if (self)
                    self->tileReady(generation, key, tile, full, mid, low);
            },
            Qt::QueuedConnection);
    }),
                priority);
}

void MapView::tileReady(int generation, quint64 key, std::shared_ptr<justnbt::RenderedTile> tile, QImage full, QImage mid,
                        QImage low)
{
    if (generation != generation_ || !regions_.contains(key))
        return;
    Region& r = regions_[key];
    r.pending = false;
    if (!r.rendered) {
        ++ready_;
        failedChunks_ += tile->failedChunks;
        r.rendered = true;
    }
    r.failedChunks = tile->failedChunks;
    low_.insert(key, new QImage(low));
    mid_.insert(key, new QImage(mid));
    full_.insert(key, new FullTile{full, tile});
    emitProgress();
    update();
}

void MapView::trimCaches(const QSet<quint64>& visible, const QSet<quint64>& nearby)
{
    for (const quint64 key : full_.keys())
        if (!visible.contains(key))
            full_.remove(key);
    for (const quint64 key : mid_.keys())
        if (!nearby.contains(key))
            mid_.remove(key);
}

void MapView::emitProgress()
{
    emit progressChanged(ready_, listing_ ? -1 : int(regions_.size()), failedChunks_);
}

void MapView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0x26, 0x28, 0x2c));

    const double s = scale();
    const double tilePx = kTile * s;
    const QPointF topLeft = toBlock(QPointF(0, 0));
    const QPointF bottomRight = toBlock(QPointF(width(), height()));
    const int rx0 = floorDiv(topLeft.x(), kTile), rx1 = floorDiv(bottomRight.x(), kTile);
    const int rz0 = floorDiv(topLeft.y(), kTile), rz1 = floorDiv(bottomRight.y(), kTile);
    const double cxr = centerX_ / kTile, czr = centerZ_ / kTile;

    p.setRenderHint(QPainter::SmoothPixmapTransform, s < 1.0);
    QSet<quint64> visible;
    for (int rz = rz0; rz <= rz1; ++rz) {
        for (int rx = rx0; rx <= rx1; ++rx) {
            const quint64 key = packKey(rx, rz);
            auto it = regions_.find(key);
            if (it == regions_.end())
                continue;
            visible.insert(key);
            const QPointF origin = toScreen(double(rx) * kTile, double(rz) * kTile);
            const QRectF target(origin, QSizeF(tilePx, tilePx));

            const QImage* img = nullptr;
            bool needed = false;
            if (tilePx > 128) {
                if (FullTile* ft = full_.object(key))
                    img = &ft->image;
                else
                    needed = true;
            }
            if (!img && tilePx > 32) {
                if (QImage* m = mid_.object(key))
                    img = m;
                else
                    needed = true;
            }
            if (!img) {
                if (QImage* l = low_.object(key))
                    img = l;
            }
            if (!img)
                needed = true;

            if (needed) {
                const double dx = rx + 0.5 - cxr, dz = rz + 0.5 - czr;
                request(key, 1000000 - int(std::min(1e6, dx * dx + dz * dz)));
            }
            if (img)
                p.drawImage(target, *img);
            else
                p.fillRect(target, QColor(0x33, 0x36, 0x3b));
        }
    }

    QSet<quint64> nearby;
    for (int rz = rz0 - 1; rz <= rz1 + 1; ++rz)
        for (int rx = rx0 - 1; rx <= rx1 + 1; ++rx)
            nearby.insert(packKey(rx, rz));
    trimCaches(visible, nearby);

    if (showGrid_) {
        p.setRenderHint(QPainter::Antialiasing, false);
        if (s >= 1.0) {
            p.setPen(QColor(255, 255, 255, 40));
            const int cx0 = floorDiv(topLeft.x(), 16), cx1 = floorDiv(bottomRight.x(), 16);
            const int cz0 = floorDiv(topLeft.y(), 16), cz1 = floorDiv(bottomRight.y(), 16);
            for (int cx = cx0; cx <= cx1 + 1; ++cx) {
                const double x = toScreen(cx * 16.0, 0).x();
                p.drawLine(QPointF(x, 0), QPointF(x, height()));
            }
            for (int cz = cz0; cz <= cz1 + 1; ++cz) {
                const double y = toScreen(0, cz * 16.0).y();
                p.drawLine(QPointF(0, y), QPointF(width(), y));
            }
        }
        if (tilePx >= 24) {
            p.setPen(QColor(255, 255, 255, 110));
            for (int rx = rx0; rx <= rx1 + 1; ++rx) {
                const double x = toScreen(rx * double(kTile), 0).x();
                p.drawLine(QPointF(x, 0), QPointF(x, height()));
            }
            for (int rz = rz0; rz <= rz1 + 1; ++rz) {
                const double y = toScreen(0, rz * double(kTile)).y();
                p.drawLine(QPointF(0, y), QPointF(width(), y));
            }
        }
    }

    if (border_ && showBorder_) {
        const QRectF r(toScreen(border_->left(), border_->top()), toScreen(border_->right(), border_->bottom()));
        const QRectF view = QRectF(rect()).adjusted(-10, -10, 10, 10);
        p.setRenderHint(QPainter::Antialiasing, false);

        p.setPen(QPen(QColor(0xff, 0x3b, 0x30), 2));
        const double top = std::max(r.top(), view.top()), bottom = std::min(r.bottom(), view.bottom());
        const double left = std::max(r.left(), view.left()), right = std::min(r.right(), view.right());
        if (top <= bottom) {
            for (double x : {r.left(), r.right()})
                if (x >= view.left() && x <= view.right())
                    p.drawLine(QPointF(x, top), QPointF(x, bottom));
        }
        if (left <= right) {
            for (double y : {r.top(), r.bottom()})
                if (y >= view.top() && y <= view.bottom())
                    p.drawLine(QPointF(left, y), QPointF(right, y));
        }
    }

    for (int i = 0; i < selections_.size(); ++i) {
        const QRect& area = selections_[i];
        const QRectF r(toScreen(area.left() * 16.0, area.top() * 16.0),
                       toScreen((area.right() + 1) * 16.0, (area.bottom() + 1) * 16.0));
        const bool current = i == current_;
        p.setRenderHint(QPainter::Antialiasing, false);
        p.fillRect(r, QColor(0x3d, 0x8b, 0xff, current ? 95 : 60));
        p.setPen(QPen(current ? QColor(0xd6, 0xea, 0xff) : QColor(0x5a, 0xa9, 0xff), current ? 3 : 2));
        p.setBrush(Qt::NoBrush);
        p.drawRect(r);
    }

    if (!searchMarkers_.empty()) {
        const QRectF visible = QRectF(rect()).adjusted(-8, -8, 8, 8);
        const double r = zoom_ >= 0 ? 4.0 : zoom_ >= -2 ? 3.0 : 2.0;
        p.setRenderHint(QPainter::Antialiasing, zoom_ >= -2);
        p.setPen(QPen(QColor(0x20, 0x20, 0x20), 1));
        p.setBrush(QColor(0xff, 0xd2, 0x3f));
        for (const QPoint& b : searchMarkers_) {
            const QPointF m = toScreen(b.x() + 0.5, b.y() + 0.5);
            if (visible.contains(m))
                p.drawEllipse(m, r, r);
        }
        if (currentSearchMarker_ >= 0) {
            const QPoint& b = searchMarkers_[size_t(currentSearchMarker_)];
            const QPointF m = toScreen(b.x() + 0.5, b.y() + 0.5);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(QPen(Qt::white, 3));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(m, r + 6, r + 6);
            p.setPen(QPen(QColor(0xe0, 0x3c, 0x3c), 2));
            p.drawEllipse(m, r + 6, r + 6);
        }
    }

    if (pin_) {
        const QPointF m = toScreen(pin_->x() + 0.5, pin_->y() + 0.5);
        const QPointF head = m - QPointF(0, 18);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor(0x20, 0x20, 0x20), 3));
        p.drawLine(m, head);
        p.setPen(QPen(Qt::white, 2));
        p.setBrush(QColor(0x2f, 0x7d, 0xf6));
        p.drawEllipse(head, 7, 7);
        p.setBrush(Qt::white);
        p.setPen(Qt::NoPen);
        p.drawEllipse(m, 2.5, 2.5);
    }

    if (marker_) {
        const QPointF m = toScreen(marker_->x() + 0.5, marker_->y() + 0.5);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(Qt::white, 2));
        p.setBrush(QColor(0xe0, 0x3c, 0x3c));
        p.drawEllipse(m, 6, 6);
    }
}

void MapView::mousePressEvent(QMouseEvent* event)
{
    const bool pan = event->button() == Qt::MiddleButton || (event->button() == Qt::LeftButton && !selectMode_);
    if (pan) {
        dragging_ = true;
        lastMouse_ = event->position().toPoint();
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (event->button() == Qt::LeftButton && selectMode_) {
        const QPoint pos = event->position().toPoint();
        const bool addAnother = event->modifiers() & Qt::ShiftModifier;
        if (const Handle h = handleAt(pos); h.valid()) {
            resizing_ = h;
            resizeBase_ = selections_[h.index];
            current_ = h.index;
            update();
            return;
        }
        if (const int inside = rectangleAt(pos); inside >= 0 && !addAnother) {
            moving_ = inside;
            moveStart_ = chunkAt(event->position());
            resizeBase_ = selections_[inside];
            current_ = inside;
            setCursor(Qt::SizeAllCursor);
            update();
            return;
        }
        selectionAnchor_ = chunkAt(event->position());
        if (!addAnother)
            selections_.clear();
        selections_.append(QRect(selectionAnchor_, QSize(1, 1)));
        current_ = int(selections_.size()) - 1;
        selecting_ = true;
        emit selectionChanged(selections_);
        update();
    }
}

void MapView::mouseMoveEvent(QMouseEvent* event)
{
    const QPoint pos = event->position().toPoint();
    if (resizing_.valid()) {
        const QPointF b = toBlock(event->position());
        const int cx = floorDiv(std::floor(b.x()), 16), cz = floorDiv(std::floor(b.y()), 16);
        QRect r = resizeBase_;
        if (resizing_.left)
            r.setLeft(std::min(cx, resizeBase_.right()));
        if (resizing_.right)
            r.setRight(std::max(cx, resizeBase_.left()));
        if (resizing_.top)
            r.setTop(std::min(cz, resizeBase_.bottom()));
        if (resizing_.bottom)
            r.setBottom(std::max(cz, resizeBase_.top()));
        selections_[resizing_.index] = r.normalized();
        emit selectionChanged(selections_);
        update();
        updateHover(event->position());
        return;
    }
    if (moving_ >= 0 && moving_ < selections_.size()) {
        const QRect moved = resizeBase_.translated(chunkAt(event->position()) - moveStart_);
        if (moved != selections_[moving_]) {
            selections_[moving_] = moved;
            emit selectionChanged(selections_);
            update();
        }
        updateHover(event->position());
        return;
    }
    if (!dragging_ && !selecting_ && selectMode_)
        updateSelectCursor(pos);
    if (selecting_ && !selections_.isEmpty()) {
        const QPointF b = toBlock(event->position());
        const QPoint corner(floorDiv(std::floor(b.x()), 16), floorDiv(std::floor(b.y()), 16));
        selections_.last() =
            QRect(QPoint(std::min(selectionAnchor_.x(), corner.x()), std::min(selectionAnchor_.y(), corner.y())),
                  QPoint(std::max(selectionAnchor_.x(), corner.x()), std::max(selectionAnchor_.y(), corner.y())));
        emit selectionChanged(selections_);
        update();
    }
    if (dragging_) {
        const QPoint delta = pos - lastMouse_;
        lastMouse_ = pos;
        centerX_ -= delta.x() / scale();
        centerZ_ -= delta.y() / scale();
        update();
    }
    updateHover(event->position());
}

void MapView::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && resizing_.valid()) {
        resizing_ = {};
        return;
    }
    if (event->button() == Qt::LeftButton && moving_ >= 0) {
        moving_ = -1;
        updateSelectCursor(event->position().toPoint());
        return;
    }
    if (event->button() == Qt::LeftButton && selecting_) {
        selecting_ = false;
        return;
    }
    if (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton) {
        dragging_ = false;
        setCursor(selectMode_ ? Qt::CrossCursor : Qt::OpenHandCursor);
    }
}

void MapView::wheelEvent(QWheelEvent* event)
{
    const int steps = event->angleDelta().y() / 120;
    if (steps == 0)
        return;
    const QPointF anchor = toBlock(event->position());
    const int old = zoom_;
    setZoom(zoom_ + (steps > 0 ? 1 : -1));
    if (zoom_ == old)
        return;
    centerX_ = anchor.x() - (event->position().x() - width() / 2.0) / scale();
    centerZ_ = anchor.y() - (event->position().y() - height() / 2.0) / scale();
    updateHover(event->position());
    update();
}

void MapView::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;
    const QPointF b = toBlock(event->position());
    emit chunkActivated(floorDiv(std::floor(b.x()), 16), floorDiv(std::floor(b.y()), 16));
}

void MapView::contextMenuEvent(QContextMenuEvent* event)
{
    const QPointF b = toBlock(QPointF(event->pos()));
    emit chunkMenuRequested(floorDiv(std::floor(b.x()), 16), floorDiv(std::floor(b.y()), 16), event->globalPos());
}

void MapView::leaveEvent(QEvent*)
{
    emit hoverInfo(QString());
}

void MapView::updateHover(const QPointF& pos)
{
    const QPointF b = toBlock(pos);
    const int bx = int(std::floor(b.x())), bz = int(std::floor(b.y()));
    const int rx = floorDiv(bx, kTile), rz = floorDiv(bz, kTile);
    QString text = tr("X %1   Z %2   ·   chunk %3, %4   ·   r.%5.%6.mca")
                       .arg(bx)
                       .arg(bz)
                       .arg(floorDiv(bx, 16))
                       .arg(floorDiv(bz, 16))
                       .arg(rx)
                       .arg(rz);
    if (border_ && !border_->contains(QPointF(bx + 0.5, bz + 0.5)))
        text += tr("   ·   outside the world border");
    const quint64 key = packKey(rx, rz);
    if (!regions_.contains(key)) {
        text += tr("   ·   no data");
    } else if (FullTile* ft = full_.object(key)) {
        const size_t i = size_t((bz - rz * kTile) * kTile + (bx - rx * kTile));
        const int16_t h = ft->data->heights[i];
        const uint16_t id = ft->data->blocks[i];
        if (h == justnbt::RenderedTile::kNoHeight)
            text += tr("   ·   empty");
        else
            text += QStringLiteral("   ·   Y %1   %2")
                        .arg(h)
                        .arg(id < ft->data->names.size() ? QString::fromStdString(ft->data->names[id]) : QString());
    }
    emit hoverInfo(text);
}
