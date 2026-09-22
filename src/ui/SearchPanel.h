#pragma once

#include "core/WorldSearch.h"

#include <QList>
#include <QPair>
#include <QPointer>
#include <QRect>
#include <QWidget>

#include <atomic>
#include <memory>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QSortFilterProxyModel;
class QTableView;
class QThread;
class SearchResultsModel;

class SearchPanel : public QWidget {
    Q_OBJECT

public:
    explicit SearchPanel(const justnbt::WorldInfo& world, QWidget* parent = nullptr);
    ~SearchPanel() override;

    void setDimension(const justnbt::Dimension& dimension);
    void setSelections(const QList<QRect>& areas);
    void search(justnbt::SearchKind kind, const QString& text, bool selectedAreaOnly = false);
    bool isSearching() const { return !thread_.isNull(); }
    const std::vector<justnbt::SearchHit>& hits() const;
    void focusQuery();
    void clearResults();

    static QString hitName(justnbt::SearchKind kind, const justnbt::SearchHit& hit);
    static QString hitDetails(justnbt::SearchKind kind, const justnbt::SearchHit& hit);
    static QString chunkFolder(const justnbt::SearchHit& hit);

signals:
    void markersChanged(const std::vector<QPoint>& blocks);
    void currentHitChanged(int index, int blockX, int blockZ);
    void openChunk(int chunkX, int chunkZ, const QString& folder);
    void searchFinished();

private:
    void startOrStop();
    void startSearch();
    void finish(std::shared_ptr<justnbt::SearchResult> result, int searchId);
    void kindChanged();
    QStringList patternsFor(const QString& text) const;
    int hitIndex(const QModelIndex& viewIndex) const;
    void showMenu(const QPoint& pos);
    void setBusy(bool busy);

    justnbt::WorldInfo world_;
    justnbt::Dimension dimension_;
    QList<QRect> selections_;
    justnbt::SearchKind kind_ = justnbt::SearchKind::Block;
    QList<QPair<QString, QString>> names_;

    QComboBox* kindBox_;
    QLineEdit* query_;
    QCheckBox* selectedOnly_;
    QPushButton* button_;
    QProgressBar* progress_;
    QLabel* status_;
    QTableView* table_;
    SearchResultsModel* model_;
    QSortFilterProxyModel* sorted_;

    QPointer<QThread> thread_;
    std::shared_ptr<std::atomic<bool>> cancel_;
    int searchId_ = 0;
};
