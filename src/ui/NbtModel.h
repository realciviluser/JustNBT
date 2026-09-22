#pragma once

#include "core/Nbt.h"

#include <QAbstractItemModel>
#include <QHash>
#include <QIcon>
#include <QSet>
#include <QStyledItemDelegate>

#include <vector>

class QUndoStack;

class NbtModel : public QAbstractItemModel {
    Q_OBJECT

public:
    enum Column { NameColumn, TypeColumn, ValueColumn, ColumnCount };

    NbtModel(nbt::Tag* root, QString rootLabel, QUndoStack* undoStack, QObject* parent = nullptr);

    nbt::Tag* tagFromIndex(const QModelIndex& index) const;
    QUndoStack* undoStack() const { return undo_; }
    nbt::Tag* rootTag() const { return root_; }
    QModelIndex indexFromTag(const nbt::Tag* tag, int column = NameColumn) const;

    QModelIndex insertTag(const QModelIndex& parent, int row, std::unique_ptr<nbt::Tag> tag);
    void removeTag(const QModelIndex& index);
    void replaceValue(nbt::Tag* tag, const nbt::Tag& newValue);

    bool insertTags(nbt::Tag* parent, int row, std::vector<std::unique_ptr<nbt::Tag>> tags, const QString& what);
    void removeTags(const std::vector<nbt::Tag*>& tags);
    void duplicateTags(const std::vector<nbt::Tag*>& tags);

    static const char* mimeType();
    static QByteArray serializeTags(const std::vector<const nbt::Tag*>& tags);
    static std::vector<std::unique_ptr<nbt::Tag>> deserializeTags(const QByteArray& data);

    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QModelIndexList& indexes) const override;
    bool canDropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column,
                         const QModelIndex& parent) const override;
    bool dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column,
                      const QModelIndex& parent) override;
    Qt::DropActions supportedDropActions() const override;
    Qt::DropActions supportedDragActions() const override;

    nbt::Tag* rawInsert(nbt::Tag* parent, int row, std::unique_ptr<nbt::Tag> tag);
    std::unique_ptr<nbt::Tag> rawTake(nbt::Tag* tag);
    void rawRename(nbt::Tag* tag, const std::string& name);
    void rawAssignValue(nbt::Tag* tag, const nbt::Tag& value);

    enum class Change { None, Added, Modified, Inside, Deleted };
    Change changeOf(const nbt::Tag* tag) const;
    bool isDeleted(const nbt::Tag* tag) const { return deletedTags_.contains(tag); }
    int addedCount() const { return added_; }
    int modifiedCount() const { return modified_; }
    int removedCount() const { return removed_; }
    const std::vector<nbt::Tag*>& changedTags() const { return changedOrder_; }
    void resetBaseline();
    void setQuiet(bool quiet) { quiet_ = quiet; }

    static QString valueText(const nbt::Tag& tag);
    static QIcon typeIcon(nbt::TagType type);
    static QString displayName(const nbt::Tag& tag);

    QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
    QModelIndex parent(const QModelIndex& index) const override;
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

signals:
    void editRejected(const QString& reason);
    void tagTouched(nbt::Tag* tag);
    void changesUpdated();

private:
    bool parseValue(const QString& text, nbt::Tag& value);
    void emitRowChanged(const nbt::Tag* tag);
    void recomputeChanges();
    void walkChanges(nbt::Tag* current, const nbt::Tag* baseline);
    std::vector<nbt::Tag*> rowsOf(const nbt::Tag* parent) const;
    const nbt::Tag* parentOf(const nbt::Tag* tag) const;
    void addDeletedRow(nbt::Tag* parent, int row, const nbt::Tag& tag, const nbt::Tag* original);
    bool dropDeletedRow(const nbt::Tag* original);
    void dropDeletedRowsUnder(const nbt::Tag* subtree);
    void clearDeletedRows();

    nbt::Tag* root_;
    QString rootLabel_;
    QUndoStack* undo_;

    std::unique_ptr<nbt::Tag> baseline_;
    QHash<const nbt::Tag*, Change> changes_;
    QHash<const nbt::Tag*, QString> previousValue_;
    QSet<const nbt::Tag*> inTree_;
    QSet<const nbt::Tag*> savedTags_;
    mutable std::vector<nbt::Tag*> dragged_;

    struct DeletedRow {
        std::shared_ptr<nbt::Tag> copy;
        const nbt::Tag* original = nullptr;
        int row = 0;
    };
    QHash<const nbt::Tag*, std::vector<DeletedRow>> deletedRows_;
    QHash<const nbt::Tag*, const nbt::Tag*> deletedOwner_;
    QSet<const nbt::Tag*> deletedTags_;
    std::vector<nbt::Tag*> changedOrder_;
    int added_ = 0, modified_ = 0, removed_ = 0;
    bool quiet_ = false;
};

class NbtItemDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
};

void copyValue(nbt::Tag& dst, const nbt::Tag& src);
