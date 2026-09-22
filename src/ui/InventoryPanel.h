#pragma once

#include "ui/Inventory.h"

#include <QPointer>
#include <QWidget>

#include <vector>

class NbtModel;
class QComboBox;
class QDoubleSpinBox;
class QGridLayout;
class QLabel;
class QSpinBox;
class QVBoxLayout;

namespace justnbt {
class SlotWidget;
class PipBar;
class XpBar;
}

class InventoryPanel : public QWidget {
    Q_OBJECT

public:
    explicit InventoryPanel(QWidget* parent = nullptr);

    void setModel(NbtModel* model);
    void showHolder(nbt::Tag* holder);
    nbt::Tag* holder() const { return layout_.holder; }
    const justnbt::InventoryLayout& layout() const { return layout_; }
    void reloadPictures();
    void setCurrentTag(const nbt::Tag* tag);

    std::vector<justnbt::SlotWidget*> slotWidgets() const { return slots_; }
    justnbt::PipBar* hearts() const { return hearts_; }
    justnbt::PipBar* food() const { return food_; }

signals:
    void revealTag(nbt::Tag* tag);
    void holderChanged(bool shown);
    void showOnMap(const QString& dimensionId, int x, int z);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void scheduleRefresh();
    void refresh();
    void rebuild();
    void updateValues();
    void moveItem(int fromSlot, int toSlot);
    void slotMenu(int index, const QPoint& globalPos);
    QString title() const;
    QString shapeKey() const;
    const justnbt::InventorySlot& slotAt(int index) const;
    int hotbarStart() const;
    void editEffect(int index);
    void editItem(int index);
    bool position(QString* dimension, int* x, int* z) const;

    NbtModel* model_ = nullptr;
    justnbt::InventoryLayout layout_;
    std::vector<justnbt::InventorySlot> allSlots_;
    std::vector<justnbt::SlotWidget*> slots_;
    QString shape_;
    bool refreshPending_ = false;
    const nbt::Tag* currentTag_ = nullptr;

    QVBoxLayout* contentLayout_;
    QWidget* content_ = nullptr;
    QLabel* title_ = nullptr;
    QLabel* empty_;
    justnbt::PipBar* hearts_ = nullptr;
    justnbt::PipBar* food_ = nullptr;
    QLabel* healthText_ = nullptr;
    QLabel* foodText_ = nullptr;
    justnbt::XpBar* xp_ = nullptr;
    QSpinBox* level_ = nullptr;
    QDoubleSpinBox* saturation_ = nullptr;
    QComboBox* gameMode_ = nullptr;
    struct EffectRow {
        QLabel* icon;
        QLabel* text;
    };
    std::vector<EffectRow> effectRows_;
};

namespace justnbt {
class SlotWidget : public QWidget {
    Q_OBJECT

public:
    SlotWidget(int index, SlotHint hint, QWidget* parent = nullptr);

    int index() const { return index_; }
    void setItem(const nbt::Tag* item);
    bool isEmpty() const { return id_.isEmpty(); }
    QString itemId() const { return id_; }
    void setCurrent(bool current);
    void setSelectedHotbar(bool selected);
    static constexpr int kSize = 36;

signals:
    void clicked(int index);
    void dropped(int from, int to);
    void menuRequested(int index, const QPoint& globalPos);
    void doubleClicked(int index);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    int index_;
    SlotHint hint_;
    QString id_;
    int count_ = 0;
    double durability_ = -1;
    bool enchanted_ = false;
    bool current_ = false;
    bool selectedHotbar_ = false;
    bool hover_ = false;
    bool dropTarget_ = false;
    QPoint pressPos_;
};

class PipBar : public QWidget {
    Q_OBJECT

public:
    enum class Kind { Hearts, Food };
    explicit PipBar(Kind kind, QWidget* parent = nullptr);

    void setValue(double value);
    void setMaximum(double maximum);
    double value() const { return value_; }
    double valueAt(const QPoint& pos) const;
    QSize sizeHint() const override;

signals:
    void valueChosen(double value);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    int pips() const;
    QRect pipRect(int i) const;

    Kind kind_;
    double value_ = 0, maximum_ = 20;
    double preview_ = -1;
};

class XpBar : public QWidget {
    Q_OBJECT

public:
    explicit XpBar(QWidget* parent = nullptr);
    void setProgress(double progress);
    void setLevel(int level);
    QSize sizeHint() const override;

signals:
    void progressChosen(double progress);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    double progress_ = 0;
    int level_ = 0;
};
}
