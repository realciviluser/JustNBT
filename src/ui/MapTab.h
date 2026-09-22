#pragma once

#include "core/Worlds.h"

#include <QComboBox>
#include <QWidget>

#include <optional>

class MapView;
class SearchPanel;
class QCheckBox;
class QComboBox;
class QLabel;
class QSpinBox;

class MapTab : public QWidget {
    Q_OBJECT

public:
    MapTab(const justnbt::WorldInfo& world, const QString& dimensionId, QWidget* parent = nullptr);

    static QString identityFor(const justnbt::WorldInfo& world) { return world.path + QStringLiteral("#map"); }
    void showDimension(const QString& dimensionId);
    void showPlace(int x, int z);
    void clearMarks();
    SearchPanel* searchPanel() const { return search_; }
    void showSearch(bool show);
    QString currentDimensionId() const { return dimension_->currentData().toString(); }
    void refresh() { applyDimension(dimension_->currentIndex(), false); }

signals:
    void openChunk(const justnbt::Dimension& dimension, int chunkX, int chunkZ, const QString& kind);

private:
    void showChunkMenu(int chunkX, int chunkZ, const QPoint& globalPos);
    void selectionChanged(const QList<QRect>& areas);
    void deleteSelectedChunks();
    const justnbt::Dimension* currentDimension() const;
    void applyDimension(int index, bool recenter);
    void applySlice();
    void showBorder(const justnbt::Dimension& dimension);
    void goToSpawn();
    void updateZoomLabel(int level);

    justnbt::WorldInfo world_;
    std::optional<QPointF> spawn_;
    bool centerWhenListed_ = false;
    MapView* view_;
    QComboBox* dimension_;
    QLabel* zoomLabel_;
    QCheckBox* grid_;
    QCheckBox* border_;
    QCheckBox* slice_;
    QCheckBox* select_;
    class ElidingLabel* selectionInfo_;
    class QToolButton* deleteButton_;
    QSpinBox* sliceY_;
    class ElidingLabel* hover_;
    class ElidingLabel* progress_;
    class ElidingLabel* borderInfo_;
    SearchPanel* search_;
    class QToolButton* searchButton_;
};
