#include "ui/NbtEditor.h"

#include "core/Config.h"
#include "core/Worlds.h"
#include "ui/Dialogs.h"
#include "ui/ElidingLabel.h"
#include "ui/InventoryPanel.h"
#include "ui/NbtModel.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QDir>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSplitter>
#include <QMenu>
#include <QMessageBox>
#include <QToolBar>
#include <QTreeView>
#include <QUndoStack>
#include <QVBoxLayout>

using nbt::Tag;
using nbt::TagType;

NbtEditor::NbtEditor(std::unique_ptr<justnbt::NbtFile> file, QString backupSubdir, QString lockFile, QWidget* parent)
    : QWidget(parent), file_(std::move(file)), backupSubdir_(std::move(backupSubdir)), lockFile_(std::move(lockFile))
{
    undo_ = new QUndoStack(this);
    model_ = new NbtModel(file_->root.get(), file_->displayName(), undo_, this);

    view_ = new QTreeView;
    view_->setModel(model_);
    view_->setItemDelegate(new NbtItemDelegate(view_));
    view_->setUniformRowHeights(true);
    view_->setAlternatingRowColors(true);
    view_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed
                           | QAbstractItemView::SelectedClicked);
    view_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    view_->setSelectionBehavior(QAbstractItemView::SelectRows);
    view_->setDragEnabled(true);
    view_->setAcceptDrops(true);
    view_->setDropIndicatorShown(true);
    view_->setDragDropMode(QAbstractItemView::DragDrop);
    view_->setDefaultDropAction(Qt::MoveAction);
    view_->setContextMenuPolicy(Qt::CustomContextMenu);
    view_->header()->setStretchLastSection(true);
    view_->expandToDepth(1);
    view_->resizeColumnToContents(NbtModel::NameColumn);
    view_->setColumnWidth(NbtModel::NameColumn, std::max(view_->columnWidth(NbtModel::NameColumn) + 24, 220));
    view_->setColumnWidth(NbtModel::TypeColumn, 90);
    view_->setCurrentIndex(model_->index(0, 0));

    auto* toolbar = new QToolBar;
    toolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);

    auto makeAction = [this](const QString& text, const QKeySequence& key, auto slot) {
        auto* a = new QAction(text, this);
        a->setShortcut(key);
        a->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        connect(a, &QAction::triggered, this, slot);
        addAction(a);
        return a;
    };
    saveAction_ = makeAction(tr("Save"), QKeySequence::Save, [this] { save(); });
    reloadAction_ = makeAction(tr("⟳ Refresh"), QKeySequence::Refresh, [this] { reload(); });
    reloadAction_->setToolTip(tr("Read it again without closing the tab (F5)"));
    addAction_ = makeAction(tr("Add"), QKeySequence(Qt::Key_Insert), [this] { addTag(); });
    removeAction_ = makeAction(tr("Delete"), QKeySequence::Delete, [this] { removeTag(); });
    copyAction_ = makeAction(tr("Copy"), QKeySequence::Copy, [this] { copySelection(false); });
    cutAction_ = makeAction(tr("Cut"), QKeySequence::Cut, [this] { copySelection(true); });
    pasteAction_ = makeAction(tr("Paste"), QKeySequence::Paste, [this] { pasteTags(); });
    duplicateAction_ = makeAction(tr("Duplicate"), QKeySequence(Qt::CTRL | Qt::Key_D),
                                  [this] { duplicateSelection(); });
    editArrayAction_ = makeAction(tr("Edit array…"), QKeySequence(),
                                  [this] { editArray(view_->currentIndex()); });
    auto* expandAction = makeAction(tr("Expand all"), QKeySequence(), [this] { view_->expandAll(); });
    auto* collapseAction = makeAction(tr("Collapse"), QKeySequence(), [this] {
        view_->collapseAll();
        view_->expand(model_->index(0, 0));
    });
    nextChangeAction_ = makeAction(tr("Next change"), QKeySequence(), [this] { goToNextChange(); });
    nextChangeAction_->setToolTip(tr("Go to the next change"));
    nextChangeAction_->setEnabled(false);
    auto* findAction = makeAction(tr("Find"), QKeySequence::Find, [this] {
        search_->setFocus();
        search_->selectAll();
    });
    Q_UNUSED(findAction);

    inventory_ = new InventoryPanel;
    inventoryAction_ = new QAction(tr("Inventory"), this);
    inventoryAction_->setCheckable(true);
    inventoryAction_->setChecked(justnbt::settings().value(QStringLiteral("editor/inventory"), true).toBool());
    inventoryAction_->setToolTip(tr("Show the inventory of the selected player, mob or container as slots"));
    connect(inventoryAction_, &QAction::toggled, this, [this](bool on) {
        justnbt::settings().setValue(QStringLiteral("editor/inventory"), on);
        updateInventoryVisibility();
    });
    connect(inventory_, &InventoryPanel::holderChanged, this, &NbtEditor::updateInventoryVisibility);
    connect(inventory_, &InventoryPanel::revealTag, this, &NbtEditor::reveal);

    undoAction_ = makeAction(tr("↶ Undo"), QKeySequence(), [this] { undo_->undo(); });
    redoAction_ = makeAction(tr("↷ Redo"), QKeySequence(), [this] { undo_->redo(); });
    undoAction_->setEnabled(false);
    redoAction_->setEnabled(false);
    undoAction_->setToolTip(tr("Undo (Ctrl+Z)"));
    redoAction_->setToolTip(tr("Redo (Ctrl+Y)"));
    connect(undo_, &QUndoStack::canUndoChanged, undoAction_, &QAction::setEnabled);
    connect(undo_, &QUndoStack::canRedoChanged, redoAction_, &QAction::setEnabled);
    connect(undo_, &QUndoStack::undoTextChanged, this, [this](const QString& text) {
        undoAction_->setToolTip(text.isEmpty() ? tr("Undo (Ctrl+Z)")
                                               : tr("Undo: %1 (Ctrl+Z)").arg(text));
    });
    connect(undo_, &QUndoStack::redoTextChanged, this, [this](const QString& text) {
        redoAction_->setToolTip(text.isEmpty() ? tr("Redo (Ctrl+Y)")
                                               : tr("Redo: %1 (Ctrl+Y)").arg(text));
    });

    saveAction_->setEnabled(false);
    toolbar->addAction(saveAction_);
    toolbar->addAction(reloadAction_);
    toolbar->addSeparator();
    toolbar->addAction(undoAction_);
    toolbar->addAction(redoAction_);
    toolbar->addSeparator();
    toolbar->addAction(addAction_);
    toolbar->addAction(removeAction_);
    toolbar->addSeparator();
    toolbar->addAction(copyAction_);
    toolbar->addAction(pasteAction_);
    toolbar->addAction(duplicateAction_);
    toolbar->addSeparator();
    toolbar->addAction(expandAction);
    toolbar->addAction(collapseAction);
    toolbar->addSeparator();
    toolbar->addAction(nextChangeAction_);
    toolbar->addSeparator();
    toolbar->addAction(inventoryAction_);
    changesLabel_ = new QLabel;
    changesLabel_->setContentsMargins(8, 0, 8, 0);
    changesLabel_->setToolTip(tr("Green marks what was added, amber what was changed.\nThe marks disappear once the "
                                 "file is saved."));
    toolbar->addWidget(changesLabel_);

    search_ = new QLineEdit;
    search_->setPlaceholderText(tr("Search by name or value (Enter — next)"));
    search_->setClearButtonEnabled(true);
    search_->setMaximumWidth(360);
    connect(search_, &QLineEdit::returnPressed, this, &NbtEditor::findNext);

    auto* spacer = new QWidget;
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(spacer);
    toolbar->addWidget(search_);

    sourceLabel_ = new ElidingLabel;
    sourceLabel_->setText(file_->sourceText());
    sourceLabel_->setContentsMargins(6, 2, 6, 2);
    QPalette pal = sourceLabel_->palette();
    pal.setColor(QPalette::WindowText, pal.color(QPalette::PlaceholderText));
    sourceLabel_->setPalette(pal);

    auto* toolbarArea = new QScrollArea;
    toolbarArea->setWidget(toolbar);
    toolbarArea->setFrameShape(QFrame::NoFrame);
    toolbarArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    toolbarArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    toolbarArea->setFixedHeight(toolbar->sizeHint().height()
                                + toolbarArea->horizontalScrollBar()->sizeHint().height());

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(toolbarArea);
    auto* splitter = new QSplitter;
    splitter->addWidget(view_);
    splitter->addWidget(inventory_);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    splitter->setCollapsible(0, false);
    splitter->setSizes({700, inventory_->minimumWidth() + 20});
    layout->addWidget(splitter, 1);
    layout->addWidget(sourceLabel_);

    connect(undo_, &QUndoStack::cleanChanged, this, [this](bool clean) { setModified(!clean); });
    connect(view_, &QTreeView::doubleClicked, this, [this](const QModelIndex& index) {
        if (const Tag* t = model_->tagFromIndex(index); t && nbt::isArray(t->type))
            editArray(index);
    });
    connect(view_, &QTreeView::customContextMenuRequested, this, &NbtEditor::showContextMenu);
    inventory_->setModel(model_);
    connectModel();
    updateActions();
    updateChangesSummary();
    updateInventoryHolder();
    updateInventoryVisibility();
}

void NbtEditor::connectModel()
{
    connect(model_, &NbtModel::tagTouched, this, &NbtEditor::reveal);
    connect(model_, &NbtModel::changesUpdated, this, &NbtEditor::updateChangesSummary);
    connect(model_, &NbtModel::editRejected, this, &NbtEditor::statusMessage);
    connect(view_->selectionModel(), &QItemSelectionModel::currentChanged, this, &NbtEditor::updateActions);
    connect(view_->selectionModel(), &QItemSelectionModel::selectionChanged, this, &NbtEditor::updateActions);
    connect(view_->selectionModel(), &QItemSelectionModel::currentChanged, this, &NbtEditor::updateInventoryHolder);
}

void NbtEditor::updateInventoryHolder()
{
    Tag* t = model_->tagFromIndex(view_->currentIndex());
    if (t && model_->isDeleted(t))
        return;
    Tag* holder = justnbt::findInventoryHolder(t);
    if (!holder && !inventory_->holder())
        holder = justnbt::levelDatPlayer(file_->root.get());
    if (holder)
        inventory_->showHolder(holder);
    inventory_->setCurrentTag(t);
}

void NbtEditor::updateInventoryVisibility()
{
    inventory_->setVisible(inventoryAction_->isChecked() && inventory_->holder());
}

void NbtEditor::refreshView()
{
    view_->viewport()->update();
    inventory_->reloadPictures();
}

QStringList NbtEditor::expandedRows() const
{
    QStringList rows;
    auto walk = [&](auto&& self, const QModelIndex& index, const QString& path) -> void {
        if (!view_->isExpanded(index))
            return;
        rows << path;
        for (int i = 0, n = model_->rowCount(index); i < n; ++i)
            self(self, model_->index(i, 0, index), path + QLatin1Char('/') + QString::number(i));
    };
    for (int i = 0, n = model_->rowCount(); i < n; ++i)
        walk(walk, model_->index(i, 0), QString::number(i));
    return rows;
}

void NbtEditor::applyExpandedRows(const QStringList& rows)
{
    for (const QString& path : rows) {
        QModelIndex index;
        for (const QString& part : path.split(QLatin1Char('/'))) {
            index = model_->index(part.toInt(), 0, index);
            if (!index.isValid())
                break;
        }
        if (index.isValid())
            view_->expand(index);
    }
}

void NbtEditor::reload()
{
    if (modified_
        && QMessageBox::question(this, tr("Refresh"),
                                 tr("Read %1 again?\n\nUnsaved changes will be lost.")
                                     .arg(file_->displayName()),
                                 QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel)
            != QMessageBox::Yes)
        return;

    std::unique_ptr<justnbt::NbtFile> fresh;
    try {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        fresh = file_->reloaded();
        QApplication::restoreOverrideCursor();
    } catch (const std::exception& e) {
        QApplication::restoreOverrideCursor();
        QMessageBox::critical(this, tr("Cannot refresh"), QString::fromUtf8(e.what()));
        return;
    }

    const QStringList open = expandedRows();
    const int scroll = view_->verticalScrollBar()->value();
    int widths[NbtModel::ColumnCount];
    for (int c = 0; c < NbtModel::ColumnCount; ++c)
        widths[c] = view_->columnWidth(c);

    file_ = std::move(fresh);
    undo_->clear();
    NbtModel* old = model_;
    model_ = new NbtModel(file_->root.get(), file_->displayName(), undo_, this);
    view_->setModel(model_);
    inventory_->setModel(model_);
    delete old;
    connectModel();
    updateInventoryHolder();

    applyExpandedRows(open);
    for (int c = 0; c < NbtModel::ColumnCount; ++c)
        view_->setColumnWidth(c, widths[c]);
    view_->verticalScrollBar()->setValue(scroll);
    view_->setCurrentIndex(model_->index(0, 0));
    sourceLabel_->setText(file_->sourceText());
    setModified(false);
    updateActions();
    updateChangesSummary();
    emit statusMessage(tr("Read again: ") + file_->displayName());
}

void NbtEditor::updateChangesSummary()
{
    const int added = model_->addedCount(), modified = model_->modifiedCount(), removed = model_->removedCount();
    QStringList parts;
    if (added)
        parts << QStringLiteral("+%1").arg(added);
    if (modified)
        parts << QStringLiteral("✎%1").arg(modified);
    if (removed)
        parts << QStringLiteral("−%1").arg(removed);
    changesLabel_->setText(parts.isEmpty() ? QString()
                                           : tr("Changes: ") + parts.join(QLatin1Char(' ')));
    nextChangeAction_->setEnabled(!model_->changedTags().empty());
    changeCursor_ = 0;
}

void NbtEditor::goToNextChange()
{
    const auto& changed = model_->changedTags();
    if (changed.empty())
        return;
    if (changeCursor_ >= changed.size())
        changeCursor_ = 0;
    reveal(changed[changeCursor_++]);
}

void NbtEditor::setModified(bool modified)
{
    if (modified_ == modified)
        return;
    modified_ = modified;
    saveAction_->setEnabled(modified);
    emit modifiedChanged(modified);
}

void NbtEditor::updateActions()
{
    const Tag* t = model_->tagFromIndex(view_->currentIndex());
    const bool deleted = t && model_->isDeleted(t);
    const auto selection = selectedTags();
    addAction_->setEnabled(t && !deleted);
    removeAction_->setEnabled(!selection.empty());
    copyAction_->setEnabled(!selection.empty());
    cutAction_->setEnabled(!selection.empty());
    duplicateAction_->setEnabled(!selection.empty());
    const QMimeData* clip = QApplication::clipboard()->mimeData();
    pasteAction_->setEnabled(t && !deleted && clip && clip->hasFormat(QString::fromLatin1(NbtModel::mimeType())));
    editArrayAction_->setEnabled(t && !deleted && nbt::isArray(t->type));
}

bool NbtEditor::save()
{
    if (!modified_)
        return true;

    if (justnbt::isLockedByGame(lockFile_)) {
        const auto answer = QMessageBox::warning(
            this, tr("The world is open in the game"),
            tr("This world is open in Minecraft right now. The game may write the file again when it closes, and "
               "your changes would be gone.\n\nClose the world in the game and save again, or save anyway."),
            QMessageBox::Save | QMessageBox::Cancel, QMessageBox::Cancel);
        if (answer != QMessageBox::Save)
            return false;
    }

    try {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        QString backup;
        try {
            backup = file_->save(backupSubdir_);
        } catch (...) {
            QApplication::restoreOverrideCursor();
            throw;
        }
        QApplication::restoreOverrideCursor();
        undo_->setClean();
        setModified(false);
        model_->resetBaseline();
        emit saved();
        if (!backup.isEmpty())
            emit statusMessage(tr("Saved. Backup: ") + QDir::toNativeSeparators(backup));
        else if (file_->isDbRecord())
            emit statusMessage(tr("Saved into the world database (its backup was already made in this run)"));
        else
            emit statusMessage(tr("Saved"));
        return true;
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Save failed"), QString::fromUtf8(e.what()));
        return false;
    }
}

void NbtEditor::addTag()
{
    QModelIndex current = view_->currentIndex().siblingAtColumn(0);
    if (!current.isValid())
        current = model_->index(0, 0);
    Tag* t = model_->tagFromIndex(current);
    if (!t)
        return;

    Tag* container = t;
    int row = int(t->children.size());
    if (!nbt::isContainer(t->type)) {
        container = t->parent;
        if (!container)
            return;
        row = container->indexOf(t) + 1;
    }

    AddTagDialog dialog(container, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const QModelIndex parentIndex = model_->indexFromTag(container);
    const QModelIndex index =
        model_->insertTag(parentIndex, row, std::make_unique<Tag>(dialog.type(), dialog.name().toStdString()));
    if (!index.isValid())
        return;

    view_->expand(parentIndex);
    view_->setCurrentIndex(index);
    view_->scrollTo(index);
    const TagType type = dialog.type();
    if (nbt::isInteger(type) || nbt::isFloating(type) || type == TagType::String)
        view_->edit(index.siblingAtColumn(NbtModel::ValueColumn));
    else if (nbt::isArray(type))
        editArray(index);
}

std::vector<Tag*> NbtEditor::selectedTags() const
{
    std::vector<Tag*> tags;
    for (const QModelIndex& index : view_->selectionModel()->selectedRows(NbtModel::NameColumn)) {
        Tag* t = model_->tagFromIndex(index);
        if (t && t->parent && !model_->isDeleted(t))
            tags.push_back(t);
    }
    std::vector<Tag*> top;
    for (Tag* t : tags) {
        bool covered = false;
        for (Tag* p = t->parent; p && !covered; p = p->parent)
            covered = std::find(tags.begin(), tags.end(), p) != tags.end();
        if (!covered)
            top.push_back(t);
    }
    auto path = [](const Tag* t) {
        std::vector<int> p;
        for (const Tag* c = t; c->parent; c = c->parent)
            p.insert(p.begin(), c->parent->indexOf(c));
        return p;
    };
    std::sort(top.begin(), top.end(), [&](const Tag* a, const Tag* b) { return path(a) < path(b); });
    return top;
}

void NbtEditor::removeTag()
{
    const auto tags = selectedTags();
    if (tags.empty())
        return;

    qsizetype inside = 0;
    for (const Tag* t : tags)
        inside += qsizetype(t->children.size());
    if (inside > 0 || tags.size() > 1) {
        const QString what = tags.size() == 1
            ? tr("%1 with everything inside it (%2)")
                  .arg(NbtModel::displayName(*tags.front()), NbtModel::valueText(*tags.front()))
            : tr("%n tag(s)", nullptr, int(tags.size()));
        if (QMessageBox::question(this, tr("Delete"), tr("Delete %1?").arg(what))
            != QMessageBox::Yes)
            return;
    }
    model_->removeTags(tags);
}

void NbtEditor::copySelection(bool cut)
{
    const auto tags = selectedTags();
    if (tags.empty())
        return;
    std::vector<const Tag*> asConst(tags.begin(), tags.end());
    auto* data = new QMimeData;
    data->setData(QString::fromLatin1(NbtModel::mimeType()), NbtModel::serializeTags(asConst));
    QApplication::clipboard()->setMimeData(data);
    const QString what = tr("%n tag(s)", nullptr, int(tags.size()));
    emit statusMessage(cut ? tr("Cut: %1").arg(what) : tr("Copied: %1").arg(what));
    if (cut)
        model_->removeTags(tags);
    updateActions();
}

void NbtEditor::pasteTags()
{
    const QMimeData* data = QApplication::clipboard()->mimeData();
    if (!data || !data->hasFormat(QString::fromLatin1(NbtModel::mimeType())))
        return;
    auto tags = NbtModel::deserializeTags(data->data(QString::fromLatin1(NbtModel::mimeType())));
    if (tags.empty())
        return;

    Tag* current = model_->tagFromIndex(view_->currentIndex());
    if (!current)
        current = model_->tagFromIndex(model_->index(0, 0));
    Tag* target = current;
    int row = -1;
    if (!nbt::isContainer(current->type) || model_->isDeleted(current)) {
        target = current->parent;
        if (!target)
            return;
        row = target->indexOf(current) + 1;
    }
    const auto count = qsizetype(tags.size());
    if (model_->insertTags(target, row, std::move(tags), tr("pasting tags: %1").arg(count))) {
        view_->expand(model_->indexFromTag(target));
        emit statusMessage(tr("Pasted: %1").arg(tr("%n tag(s)", nullptr, int(count))));
    }
}

void NbtEditor::duplicateSelection()
{
    const auto tags = selectedTags();
    if (!tags.empty())
        model_->duplicateTags(tags);
}

void NbtEditor::editArray(const QModelIndex& index)
{
    Tag* t = model_->tagFromIndex(index);
    if (!t || !nbt::isArray(t->type))
        return;
    ArrayEditDialog dialog(t, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const Tag& r = dialog.result();
    if (r.bytes != t->bytes || r.ints != t->ints || r.longs != t->longs)
        model_->replaceValue(t, r);
}

void NbtEditor::reveal(Tag* tag)
{
    for (Tag* p = tag->parent; p; p = p->parent)
        view_->expand(model_->indexFromTag(p));
    const QModelIndex index = model_->indexFromTag(tag);
    view_->setCurrentIndex(index);
    view_->scrollTo(index);
}

void NbtEditor::findNext()
{
    const QString query = search_->text().trimmed();
    if (query.isEmpty())
        return;

    std::vector<Tag*> order;
    auto collect = [&order](auto&& self, Tag* t) -> void {
        order.push_back(t);
        for (auto& c : t->children)
            self(self, c.get());
    };
    collect(collect, file_->root.get());

    auto matches = [&query](const Tag* t) {
        if (QString::fromStdString(t->name).contains(query, Qt::CaseInsensitive))
            return true;
        if (nbt::isInteger(t->type) || nbt::isFloating(t->type) || t->type == TagType::String)
            return NbtModel::valueText(*t).contains(query, Qt::CaseInsensitive);
        return false;
    };

    const Tag* current = model_->tagFromIndex(view_->currentIndex());
    size_t start = 0;
    for (size_t i = 0; i < order.size(); ++i)
        if (order[i] == current)
            start = i + 1;

    for (size_t k = 0; k < order.size(); ++k) {
        Tag* t = order[(start + k) % order.size()];
        if (!matches(t))
            continue;
        reveal(t);
        return;
    }
    emit statusMessage(tr("Nothing found: ") + query);
}

void NbtEditor::showContextMenu(const QPoint& pos)
{
    const QModelIndex index = view_->indexAt(pos);
    if (index.isValid() && !view_->selectionModel()->isSelected(index))
        view_->setCurrentIndex(index);

    QMenu menu(this);
    menu.addAction(addAction_);
    if (editArrayAction_->isEnabled())
        menu.addAction(editArrayAction_);
    menu.addSeparator();
    menu.addAction(copyAction_);
    menu.addAction(cutAction_);
    menu.addAction(pasteAction_);
    menu.addAction(duplicateAction_);
    menu.addSeparator();
    menu.addAction(removeAction_);
    menu.addSeparator();
    menu.addAction(reloadAction_);
    menu.exec(view_->viewport()->mapToGlobal(pos));
}
