#pragma once

#include "core/MapRender.h"

#include <QCache>
#include <QSet>
#include <QHash>
#include <QImage>
#include <QThreadPool>
#include <QWidget>

#include <optional>
#include <vector>

class MapView : public QWidget {
    Q_OBJECT

public:
    static constexpr int kMinZoom = -5;
    static constexpr int kMaxZoom = 4;

    explicit MapView(QWidget* parent = nullptr);
    ~MapView() override;

    void setSource(std::shared_ptr<justnbt::TileSource> source, const justnbt::RenderOptions& options);
    void setRenderOptions(const justnbt::RenderOptions& options);
    void reload();

    void centerOn(double blockX, double blockZ);
    void setZoom(int level);
    int zoom() const { return zoom_; }
    void setShowGrid(bool show);
    void setMarker(std::optional<QPointF> blockPos);
    void setPin(std::optional<QPoint> block);
    std::optional<QPoint> pin() const { return pin_; }
    void setSearchMarkers(std::vector<QPoint> blocks);
    void setCurrentSearchMarker(int index);
    int searchMarkerCount() const { return int(searchMarkers_.size()); }
    void setBorder(std::optional<QRectF> border);
    void setShowBorder(bool show);

    void setSelectMode(bool on);
    bool selectMode() const { return selectMode_; }
    const QList<QRect>& selections() const { return selections_; }
    int currentSelection() const { return current_; }
    void clearSelection();

    int regionCount() const { return int(regions_.size()); }
    QPointF regionsCenter() const;

signals:
    void gridToggleRequested();
    void hoverInfo(const QString& text);
    void progressChanged(int ready, int total, int failedChunks);
    void tilesListed();
    void chunkActivated(int chunkX, int chunkZ);
    void chunkMenuRequested(int chunkX, int chunkZ, const QPoint& globalPos);
    void zoomChanged(int level);
    void selectionChanged(const QList<QRect>& areas);
    void selectModeChanged(bool on);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    bool focusNextPrevChild(bool next) override;

private:
    struct Region {
        bool pending = false;
        bool rendered = false;
        int failedChunks = 0;
    };
    struct Handle {
        int index = -1;
        bool left = false, right = false, top = false, bottom = false;
        bool valid() const { return index >= 0; }
    };
    struct FullTile {
        QImage image;
        std::shared_ptr<justnbt::RenderedTile> data;
    };

    double scale() const;
    QPointF toScreen(double bx, double bz) const;
    QPointF toBlock(const QPointF& screen) const;
    void request(quint64 key, int priority);
    void tilesArrived(int generation, const std::vector<justnbt::TilePos>& tiles);
    void tileReady(int generation, quint64 key, std::shared_ptr<justnbt::RenderedTile> tile, QImage full, QImage mid,
                   QImage low);
    void updateHover(const QPointF& pos);
    void emitProgress();
    Handle handleAt(const QPoint& pos) const;
    int rectangleAt(const QPoint& pos) const;
    void updateSelectCursor(const QPoint& pos);
    QPoint chunkAt(const QPointF& screen) const;
    void trimCaches(const QSet<quint64>& visible, const QSet<quint64>& nearby);

    QThreadPool pool_;
    int generation_ = 0;
    std::shared_ptr<justnbt::TileSource> source_;
    bool listing_ = false;
    justnbt::RenderOptions options_;
    QHash<quint64, Region> regions_;
    QCache<quint64, FullTile> full_;
    QCache<quint64, QImage> mid_;
    QCache<quint64, QImage> low_;
    int ready_ = 0;
    int failedChunks_ = 0;

    double centerX_ = 0, centerZ_ = 0;
    int zoom_ = 0;
    bool showGrid_ = false;
    std::optional<QPointF> marker_;
    std::optional<QPoint> pin_;
    std::optional<QRectF> border_;
    bool showBorder_ = true;
    bool dragging_ = false;
    QPoint lastMouse_;
    bool selectMode_ = false;
    bool selecting_ = false;
    Handle resizing_;
    QRect resizeBase_;
    int moving_ = -1;
    QPoint moveStart_;
    int current_ = -1;
    QPoint selectionAnchor_;
    QList<QRect> selections_;
    bool ctrlAlone_ = false;
    std::vector<QPoint> searchMarkers_;
    int currentSearchMarker_ = -1;
};
