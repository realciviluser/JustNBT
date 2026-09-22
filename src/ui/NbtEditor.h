#pragma once

#include "core/NbtFile.h"

#include <QWidget>

class ElidingLabel;
class InventoryPanel;
class NbtModel;
class QAction;
class QLabel;
class QLineEdit;
class QTreeView;
class QUndoStack;
namespace nbt {
struct Tag;
}

class NbtEditor : public QWidget {
    Q_OBJECT

public:
    NbtEditor(std::unique_ptr<justnbt::NbtFile> file, QString backupSubdir, QString lockFile, QWidget* parent = nullptr);

    QString identity() const { return file_->identity(); }
    bool isModified() const { return modified_; }
    bool save();
    QUndoStack* undoStack() const { return undo_; }
    const justnbt::NbtFile& file() const { return *file_; }
    InventoryPanel* inventoryPanel() const { return inventory_; }

signals:
    void modifiedChanged(bool modified);
    void statusMessage(const QString& text);
    void saved();

public slots:
    void reload();
    void refreshView();

private:
    void connectModel();
    QStringList expandedRows() const;
    void applyExpandedRows(const QStringList& rows);
    void setModified(bool modified);
    void updateActions();
    void addTag();
    void removeTag();
    std::vector<nbt::Tag*> selectedTags() const;
    void copySelection(bool cut);
    void pasteTags();
    void duplicateSelection();
    void editArray(const QModelIndex& index);
    void reveal(nbt::Tag* tag);
    void findNext();
    void goToNextChange();
    void updateChangesSummary();
    void showContextMenu(const QPoint& pos);
    void updateInventoryHolder();
    void updateInventoryVisibility();

    std::unique_ptr<justnbt::NbtFile> file_;
    QString backupSubdir_;
    QString lockFile_;
    bool modified_ = false;

    QUndoStack* undo_;
    NbtModel* model_;
    QTreeView* view_;
    InventoryPanel* inventory_;
    QAction* inventoryAction_;
    QLineEdit* search_;
    QLabel* changesLabel_;
    ElidingLabel* sourceLabel_;
    QAction* nextChangeAction_;
    size_t changeCursor_ = 0;
    QAction* saveAction_;
    QAction* reloadAction_;
    QAction* addAction_;
    QAction* removeAction_;
    QAction* copyAction_;
    QAction* cutAction_;
    QAction* pasteAction_;
    QAction* duplicateAction_;
    QAction* editArrayAction_;
    QAction* undoAction_;
    QAction* redoAction_;
};
