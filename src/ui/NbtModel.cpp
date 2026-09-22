#include "ui/NbtModel.h"

#include "core/ItemInfo.h"
#include "core/TagInfo.h"
#include "core/NbtIO.h"
#include "ui/ItemVisuals.h"

#include <QColor>
#include <QGuiApplication>
#include <QHash>
#include <QPainter>
#include <QPalette>
#include <QMimeData>
#include <QSet>
#include <QUndoCommand>
#include <QUndoStack>

#include <charconv>
#include <cstring>
#include <limits>

using nbt::Tag;
using nbt::TagType;

namespace {
QString formatFloating(double v, bool single)
{
    char buf[64];
    const auto r = single ? std::to_chars(buf, buf + sizeof buf, float(v)) : std::to_chars(buf, buf + sizeof buf, v);
    return QString::fromLatin1(buf, int(r.ptr - buf));
}

template <class T>
QString arrayPreview(const std::vector<T>& values)
{
    constexpr size_t kPreview = 8;
    QString s = NbtModel::tr("%n element(s)", nullptr, int(values.size()));
    if (values.empty())
        return s;
    s += QStringLiteral(":  ");
    for (size_t i = 0; i < std::min(values.size(), kPreview); ++i) {
        if (i)
            s += QStringLiteral(", ");
        s += QString::number(qint64(values[i]));
    }
    if (values.size() > kPreview)
        s += QStringLiteral(", …");
    return s;
}

bool isListElement(const Tag* t) { return t->parent && t->parent->type == TagType::List; }
}

void copyValue(Tag& dst, const Tag& src)
{
    dst.integer = src.integer;
    dst.floating = src.floating;
    dst.string = src.string;
    dst.bytes = src.bytes;
    dst.ints = src.ints;
    dst.longs = src.longs;
}

namespace {
QString quoted(const Tag& t)
{
    return QStringLiteral("«%1»").arg(NbtModel::displayName(t));
}

class ValueCommand : public QUndoCommand {
public:
    ValueCommand(NbtModel* model, Tag* tag, const Tag& newValue)
        : QUndoCommand(NbtModel::tr("change %1").arg(quoted(*tag))), model_(model), tag_(tag),
          before_(tag->type), after_(tag->type)
    {
        copyValue(before_, *tag);
        copyValue(after_, newValue);
    }
    void redo() override { model_->rawAssignValue(tag_, after_); }
    void undo() override { model_->rawAssignValue(tag_, before_); }

private:
    NbtModel* model_;
    Tag* tag_;
    Tag before_, after_;
};

class RenameCommand : public QUndoCommand {
public:
    RenameCommand(NbtModel* model, Tag* tag, std::string newName)
        : QUndoCommand(NbtModel::tr("rename %1").arg(quoted(*tag))), model_(model), tag_(tag),
          before_(tag->name), after_(std::move(newName))
    {
    }
    void redo() override { model_->rawRename(tag_, after_); }
    void undo() override { model_->rawRename(tag_, before_); }

private:
    NbtModel* model_;
    Tag* tag_;
    std::string before_, after_;
};

class InsertCommand : public QUndoCommand {
public:
    InsertCommand(NbtModel* model, Tag* parent, int row, std::unique_ptr<Tag> tag)
        : QUndoCommand(), model_(model), parent_(parent), row_(row), tag_(tag.get()), holder_(std::move(tag)),
          oldListType_(parent->listType)
    {
        setText(NbtModel::tr("add %1").arg(tag_->name.empty() ? QString::fromLatin1(nbt::typeName(tag_->type))
                                                                         : QString::fromStdString(tag_->name)));
    }
    void redo() override
    {
        if (parent_->type == TagType::List && parent_->children.empty())
            parent_->listType = tag_->type;
        model_->rawInsert(parent_, row_, std::move(holder_));
    }
    void undo() override
    {
        holder_ = model_->rawTake(tag_);
        if (parent_->type == TagType::List && parent_->children.empty())
            parent_->listType = oldListType_;
    }
    Tag* tag() const { return tag_; }

private:
    NbtModel* model_;
    Tag* parent_;
    int row_;
    Tag* tag_;
    std::unique_ptr<Tag> holder_;
    TagType oldListType_;
};

class RemoveCommand : public QUndoCommand {
public:
    RemoveCommand(NbtModel* model, Tag* tag)
        : QUndoCommand(NbtModel::tr("delete %1").arg(quoted(*tag))), model_(model), parent_(tag->parent),
          row_(tag->parent->indexOf(tag)), tag_(tag)
    {
    }
    void redo() override { holder_ = model_->rawTake(tag_); }
    void undo() override { model_->rawInsert(parent_, row_, std::move(holder_)); }

private:
    NbtModel* model_;
    Tag* parent_;
    int row_;
    Tag* tag_;
    std::unique_ptr<Tag> holder_;
};
}

NbtModel::NbtModel(Tag* root, QString rootLabel, QUndoStack* undoStack, QObject* parent)
    : QAbstractItemModel(parent), root_(root), rootLabel_(std::move(rootLabel)), undo_(undoStack)
{
    resetBaseline();
}

QString NbtModel::displayName(const Tag& t)
{
    if (isListElement(&t))
        return QStringLiteral("[%1]").arg(t.parent->indexOf(&t));
    return QString::fromStdString(t.name);
}

Tag* NbtModel::tagFromIndex(const QModelIndex& index) const
{
    return index.isValid() ? static_cast<Tag*>(index.internalPointer()) : nullptr;
}

QModelIndex NbtModel::indexFromTag(const Tag* tag, int column) const
{
    if (!tag)
        return {};
    if (tag == root_)
        return createIndex(0, column, root_);
    const Tag* parent = parentOf(tag);
    if (!parent)
        return {};
    if (!deletedRows_.contains(parent))
        return createIndex(parent->indexOf(tag), column, const_cast<Tag*>(tag));
    const auto rows = rowsOf(parent);
    const auto it = std::find(rows.begin(), rows.end(), tag);
    if (it == rows.end())
        return {};
    return createIndex(int(it - rows.begin()), column, const_cast<Tag*>(tag));
}

QModelIndex NbtModel::index(int row, int column, const QModelIndex& parent) const
{
    if (row < 0 || column < 0 || column >= ColumnCount)
        return {};
    if (!parent.isValid())
        return row == 0 ? createIndex(0, column, root_) : QModelIndex();
    const Tag* p = tagFromIndex(parent);
    if (!p)
        return {};
    if (!deletedRows_.contains(p))
        return row < int(p->children.size()) ? createIndex(row, column, p->children[size_t(row)].get())
                                             : QModelIndex();
    const auto rows = rowsOf(p);
    return row < int(rows.size()) ? createIndex(row, column, rows[size_t(row)]) : QModelIndex();
}

QModelIndex NbtModel::parent(const QModelIndex& index) const
{
    const Tag* t = tagFromIndex(index);
    const Tag* p = t ? parentOf(t) : nullptr;
    return p ? indexFromTag(p) : QModelIndex();
}

int NbtModel::rowCount(const QModelIndex& parent) const
{
    if (!parent.isValid())
        return 1;
    if (parent.column() != NameColumn)
        return 0;
    const Tag* p = tagFromIndex(parent);
    const auto it = deletedRows_.constFind(p);
    return int(p->children.size() + (it == deletedRows_.constEnd() ? 0 : it->size()));
}

int NbtModel::columnCount(const QModelIndex&) const
{
    return ColumnCount;
}

QString NbtModel::valueText(const Tag& t)
{
    switch (t.type) {
    case TagType::Byte:
    case TagType::Short:
    case TagType::Int:
    case TagType::Long: return QString::number(t.integer);
    case TagType::Float: return formatFloating(t.floating, true);
    case TagType::Double: return formatFloating(t.floating, false);
    case TagType::String: return QString::fromStdString(t.string);
    case TagType::ByteArray: return arrayPreview(t.bytes);
    case TagType::IntArray: return arrayPreview(t.ints);
    case TagType::LongArray: return arrayPreview(t.longs);
    case TagType::List:
        return NbtModel::tr("%n element(s)", nullptr, int(t.children.size()))
            + (t.children.empty() ? QString() : QStringLiteral(" (") + nbt::typeName(t.listType) + ')');
    case TagType::Compound: return NbtModel::tr("%n entry(ies)", nullptr, int(t.children.size()));
    case TagType::End: return {};
    }
    return {};
}

QIcon NbtModel::typeIcon(TagType type)
{
    static QHash<int, QIcon> cache;
    if (auto it = cache.constFind(int(type)); it != cache.constEnd())
        return *it;

    struct Style {
        const char* text;
        QColor color;
    };
    static const Style styles[] = {
        {"", QColor(0x88, 0x88, 0x88)},
        {"B", QColor(0x5b, 0x8d, 0xd6)},
        {"S", QColor(0x4f, 0x9d, 0xb8)},
        {"I", QColor(0x3f, 0x8f, 0x5f)},
        {"L", QColor(0x2f, 0x7a, 0x4a)},
        {"F", QColor(0xc0, 0x7a, 0x2e)},
        {"D", QColor(0xa8, 0x5a, 0x1e)},
        {"[B]", QColor(0x7a, 0x6a, 0xc8)},
        {"T", QColor(0xc4, 0x4f, 0x6a)},
        {"[ ]", QColor(0x6c, 0x75, 0x7d)},
        {"{ }", QColor(0xb0, 0x8a, 0x1a)},
        {"[I]", QColor(0x5f, 0x4f, 0xb0)},
        {"[L]", QColor(0x4a, 0x3a, 0x96)},
    };
    const Style& st = styles[int(type)];

    constexpr int kSize = 16, kScale = 2;
    QPixmap pm(kSize * kScale, kSize * kScale);
    pm.fill(Qt::transparent);
    {
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(kScale, kScale);
        p.setPen(Qt::NoPen);
        p.setBrush(st.color);
        p.drawRoundedRect(QRectF(0.5, 1.5, kSize - 1, kSize - 3), 3, 3);
        QFont f = QGuiApplication::font();
        f.setBold(true);
        f.setPixelSize(std::strlen(st.text) > 1 ? 7 : 10);
        p.setFont(f);
        p.setPen(Qt::white);
        p.drawText(QRectF(0, 0, kSize, kSize), Qt::AlignCenter, QString::fromLatin1(st.text));
    }
    pm.setDevicePixelRatio(kScale);
    return *cache.insert(int(type), QIcon(pm));
}

QVariant NbtModel::data(const QModelIndex& index, int role) const
{
    const Tag* t = tagFromIndex(index);
    if (!t)
        return {};

    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case NameColumn:
            if (t == root_)
                return rootLabel_;
            if (deletedTags_.contains(t) && t->name.empty() && !t->parent)
                return tr("[deleted]");
            return displayName(*t);
        case TypeColumn:
            return QString::fromLatin1(nbt::typeName(t->type));
        case ValueColumn:
            if (t->type == TagType::Compound) {
                if (const auto item = justnbt::readItemStack(*t))
                    return justnbt::describeItem(*item);
                if (const auto summary = justnbt::summarizeTag(*t))
                    return summary->text;
            }
            return valueText(*t);
        }
        break;
    case Qt::EditRole:
        if (index.column() == NameColumn)
            return QString::fromStdString(t->name);
        if (index.column() == ValueColumn)
            return valueText(*t);
        break;
    case Qt::DecorationRole:
        if (index.column() == NameColumn) {
            if (t->type == TagType::Compound) {
                if (const auto item = justnbt::readItemStack(*t))
                    return justnbt::itemIcon(item->id);
                if (const auto summary = justnbt::summarizeTag(*t); summary && !summary->iconId.isEmpty())
                    return justnbt::itemIcon(summary->iconId);
            }
            return typeIcon(t->type);
        }
        break;
    case Qt::FontRole:
        if (deletedTags_.contains(t)) {
            QFont f = QGuiApplication::font();
            f.setStrikeOut(true);
            return f;
        }
        break;
    case Qt::ForegroundRole:
        if (deletedTags_.contains(t))
            return QColor(0xd0, 0x80, 0x80, 170);
        if (index.column() == TypeColumn)
            return QGuiApplication::palette().color(QPalette::PlaceholderText);
        if (index.column() == NameColumn && changeOf(t) == Change::Inside)
            return QColor(0xd0, 0x94, 0x2c);
        break;
    case Qt::BackgroundRole:
        switch (changeOf(t)) {
        case Change::Added: return QColor(0x2e, 0xa0, 0x43, 70);
        case Change::Modified: return QColor(0xe0, 0xa0, 0x20, 70);
        case Change::Deleted: return QColor(0xc0, 0x30, 0x30, 70);
        default: break;
        }
        break;
    case Qt::ToolTipRole: {
        QStringList tip;
        if (changeOf(t) == Change::Added)
            tip << tr("Added (not saved yet)");
        if (const auto before = previousValue_.constFind(t); before != previousValue_.constEnd())
            tip << tr("Changed, was: %1").arg(*before);
        if (deletedTags_.contains(t))
            tip << tr("Deleted — it goes away once the file is saved (Ctrl+Z brings it back)");
        if (index.column() == ValueColumn && (t->type == TagType::String || nbt::isArray(t->type)))
            tip << valueText(*t).left(2000);
        if (t->type == TagType::Compound) {
            if (const auto item = justnbt::readItemStack(*t))
                tip << justnbt::describeItemLines(*item);
            else if (const auto summary = justnbt::summarizeTag(*t))
                tip << summary->tooltip;
        }
        return tip.isEmpty() ? QVariant() : QVariant(tip.join(QLatin1Char('\n')));
    }
    }
    return {};
}

Qt::ItemFlags NbtModel::flags(const QModelIndex& index) const
{
    const Tag* t = tagFromIndex(index);
    if (!t)
        return Qt::NoItemFlags;
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (deletedTags_.contains(t))
        return f;
    if (t != root_ && !deletedTags_.contains(t))
        f |= Qt::ItemIsDragEnabled;
    if (nbt::isContainer(t->type) && !deletedTags_.contains(t))
        f |= Qt::ItemIsDropEnabled;
    if (index.column() == NameColumn && t->parent && t->parent->type == TagType::Compound)
        f |= Qt::ItemIsEditable;
    if (index.column() == ValueColumn && (nbt::isInteger(t->type) || nbt::isFloating(t->type) || t->type == TagType::String))
        f |= Qt::ItemIsEditable;
    return f;
}

bool NbtModel::parseValue(const QString& input, Tag& t)
{
    const QString text = input.trimmed();
    bool ok = false;

    if (nbt::isInteger(t.type)) {
        qlonglong v = 0;
        if (t.type == TagType::Byte && (text.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0
                                        || text.compare(QLatin1String("false"), Qt::CaseInsensitive) == 0)) {
            v = text.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0 ? 1 : 0;
            ok = true;
        } else {
            v = text.toLongLong(&ok);
        }
        if (!ok) {
            emit editRejected(tr("%1 is not a whole number").arg(text));
            return false;
        }
        qlonglong lo = 0, hi = 0;
        switch (t.type) {
        case TagType::Byte: lo = std::numeric_limits<int8_t>::min(), hi = std::numeric_limits<int8_t>::max(); break;
        case TagType::Short: lo = std::numeric_limits<int16_t>::min(), hi = std::numeric_limits<int16_t>::max(); break;
        case TagType::Int: lo = std::numeric_limits<int32_t>::min(), hi = std::numeric_limits<int32_t>::max(); break;
        default: lo = std::numeric_limits<qlonglong>::min(), hi = std::numeric_limits<qlonglong>::max(); break;
        }
        if (v < lo || v > hi) {
            emit editRejected(tr("%1 allows values from %2 to %3").arg(nbt::typeName(t.type)).arg(lo).arg(hi));
            return false;
        }
        t.integer = v;
        return true;
    }

    if (nbt::isFloating(t.type)) {
        QString normalized = text;
        normalized.replace(',', '.');
        const double v = t.type == TagType::Float ? double(normalized.toFloat(&ok)) : normalized.toDouble(&ok);
        if (!ok) {
            emit editRejected(tr("%1 is not a number").arg(text));
            return false;
        }
        t.floating = v;
        return true;
    }

    if (t.type == TagType::String) {
        t.string = input.toStdString();
        return true;
    }
    return false;
}

bool NbtModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    Tag* t = tagFromIndex(index);
    if (!t || role != Qt::EditRole)
        return false;

    if (index.column() == NameColumn) {
        const std::string name = value.toString().toStdString();
        if (name == t->name)
            return false;
        if (t->parent && t->parent->child(name)) {
            emit editRejected(tr("There already is a tag named %1 here").arg(value.toString()));
            return false;
        }
        undo_->push(new RenameCommand(this, t, name));
        return true;
    }

    if (index.column() == ValueColumn) {
        Tag edited(t->type);
        copyValue(edited, *t);
        if (!parseValue(value.toString(), edited))
            return false;
        if (valueText(edited) == valueText(*t))
            return false;
        undo_->push(new ValueCommand(this, t, edited));
        return true;
    }
    return false;
}

QVariant NbtModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section) {
    case NameColumn: return tr("Name");
    case TypeColumn: return tr("Type");
    case ValueColumn: return tr("Value");
    }
    return {};
}

QModelIndex NbtModel::insertTag(const QModelIndex& parentIndex, int row, std::unique_ptr<Tag> tag)
{
    Tag* p = tagFromIndex(parentIndex);
    if (!p || !nbt::isContainer(p->type))
        return {};
    if (p->type == TagType::List && !p->children.empty() && tag->type != p->listType)
        return {};

    auto* command = new InsertCommand(this, p, row, std::move(tag));
    Tag* inserted = command->tag();
    undo_->push(command);
    return indexFromTag(inserted);
}

void NbtModel::removeTag(const QModelIndex& index)
{
    Tag* t = tagFromIndex(index);
    if (!t || t == root_)
        return;
    undo_->push(new RemoveCommand(this, t));
}

void NbtModel::replaceValue(Tag* tag, const Tag& newValue)
{
    undo_->push(new ValueCommand(this, tag, newValue));
}

void NbtModel::emitRowChanged(const Tag* tag)
{
    const QModelIndex i = indexFromTag(tag);
    emit dataChanged(i, i.siblingAtColumn(ColumnCount - 1));
}

Tag* NbtModel::rawInsert(Tag* parent, int row, std::unique_ptr<Tag> tag)
{
    Tag* back = tag.get();
    int mergedRow = row;
    if (const auto it = deletedRows_.constFind(parent); it != deletedRows_.constEnd())
        for (const DeletedRow& ghost : *it)
            if (ghost.row <= row)
                ++mergedRow;
    beginInsertRows(indexFromTag(parent), mergedRow, mergedRow);
    Tag* raw = parent->insert(size_t(row), std::move(tag));
    endInsertRows();
    dropDeletedRow(back);
    emitRowChanged(parent);
    if (parent->type == TagType::List && row + 1 < int(parent->children.size()))
        emit dataChanged(index(row + 1, NameColumn, indexFromTag(parent)),
                         index(int(parent->children.size()) - 1, NameColumn, indexFromTag(parent)));
    recomputeChanges();
    if (!quiet_)
        emit tagTouched(raw);
    return raw;
}

std::unique_ptr<Tag> NbtModel::rawTake(Tag* tag)
{
    Tag* parent = tag->parent;
    const int row = parent->indexOf(tag);
    const int mergedRow = indexFromTag(tag).row();
    const bool wasSaved = savedTags_.contains(tag);

    beginRemoveRows(indexFromTag(parent), mergedRow, mergedRow);
    auto taken = parent->take(size_t(row));
    endRemoveRows();
    dropDeletedRowsUnder(taken.get());
    if (wasSaved)
        addDeletedRow(parent, row, *taken, taken.get());
    emitRowChanged(parent);
    if (parent->type == TagType::List && row < int(parent->children.size()))
        emit dataChanged(index(row, NameColumn, indexFromTag(parent)),
                         index(int(parent->children.size()) - 1, NameColumn, indexFromTag(parent)));
    recomputeChanges();
    if (!quiet_)
        emit tagTouched(parent);
    return taken;
}

void NbtModel::rawRename(Tag* tag, const std::string& name)
{
    tag->name = name;
    emitRowChanged(tag);
    recomputeChanges();
    if (!quiet_)
        emit tagTouched(tag);
}

void NbtModel::rawAssignValue(Tag* tag, const Tag& value)
{
    copyValue(*tag, value);
    emitRowChanged(tag);
    recomputeChanges();
    if (!quiet_)
        emit tagTouched(tag);
}

namespace {
bool sameValue(const Tag& a, const Tag& b)
{
    if (a.type != b.type)
        return false;
    switch (a.type) {
    case TagType::Byte:
    case TagType::Short:
    case TagType::Int:
    case TagType::Long: return a.integer == b.integer;
    case TagType::Float:
    case TagType::Double: return std::memcmp(&a.floating, &b.floating, sizeof(double)) == 0;
    case TagType::String: return a.string == b.string;
    case TagType::ByteArray: return a.bytes == b.bytes;
    case TagType::IntArray: return a.ints == b.ints;
    case TagType::LongArray: return a.longs == b.longs;
    default: return true;
    }
}
}

void NbtModel::resetBaseline()
{
    clearDeletedRows();
    baseline_ = root_->clone();
    savedTags_.clear();
    std::vector<const Tag*> stack{root_};
    while (!stack.empty()) {
        const Tag* t = stack.back();
        stack.pop_back();
        savedTags_.insert(t);
        for (const auto& c : t->children)
            stack.push_back(c.get());
    }
    recomputeChanges();
}

NbtModel::Change NbtModel::changeOf(const Tag* tag) const
{
    if (deletedTags_.contains(tag))
        return Change::Deleted;
    return changes_.value(tag, Change::None);
}

void NbtModel::walkChanges(Tag* current, const Tag* baseline)
{
    inTree_.insert(current);
    if (!baseline) {
        changes_.insert(current, Change::Added);
        changedOrder_.push_back(current);
        ++added_;
        for (auto& c : current->children)
            walkChanges(c.get(), nullptr);
        return;
    }

    if (!sameValue(*current, *baseline) || current->name != baseline->name) {
        changes_.insert(current, Change::Modified);
        previousValue_.insert(current, current->name != baseline->name
                                  ? tr("name: %1").arg(QString::fromStdString(baseline->name))
                                  : valueText(*baseline));
        changedOrder_.push_back(current);
        ++modified_;
    }

    if (current->type == TagType::Compound && baseline->type == TagType::Compound) {
        std::vector<Tag*> newNames;
        std::vector<const Tag*> goneNames;
        for (auto& c : current->children) {
            if (const Tag* b = baseline->child(c->name))
                walkChanges(c.get(), b);
            else
                newNames.push_back(c.get());
        }
        for (const auto& b : baseline->children)
            if (!current->child(b->name))
                goneNames.push_back(b.get());
        const size_t pairs = std::min(newNames.size(), goneNames.size());
        for (size_t i = 0; i < pairs; ++i)
            walkChanges(newNames[i], goneNames[i]);
        for (size_t i = pairs; i < newNames.size(); ++i)
            walkChanges(newNames[i], nullptr);
    } else if (current->type == TagType::List && baseline->type == TagType::List) {
        for (size_t i = 0; i < current->children.size(); ++i)
            walkChanges(current->children[i].get(), i < baseline->children.size() ? baseline->children[i].get() : nullptr);
    } else {
        for (auto& c : current->children)
            walkChanges(c.get(), nullptr);
    }
}

void NbtModel::recomputeChanges()
{
    const auto oldChanges = changes_;
    changes_.clear();
    previousValue_.clear();
    changedOrder_.clear();
    inTree_.clear();
    added_ = modified_ = removed_ = 0;
    if (baseline_)
        walkChanges(root_, baseline_.get());

    for (auto it = deletedRows_.cbegin(); it != deletedRows_.cend(); ++it) {
        removed_ += int(it.value().size());
        for (const DeletedRow& ghost : it.value())
            changedOrder_.push_back(ghost.copy.get());
    }

    const std::vector<Tag*> direct = changedOrder_;
    for (Tag* t : direct)
        for (const Tag* p = parentOf(t); p; p = parentOf(p))
            if (!changes_.contains(p))
                changes_.insert(p, Change::Inside);

    QSet<const Tag*> touched;
    for (auto it = oldChanges.cbegin(); it != oldChanges.cend(); ++it)
        if (changes_.value(it.key(), Change::None) != it.value())
            touched.insert(it.key());
    for (auto it = changes_.cbegin(); it != changes_.cend(); ++it)
        if (oldChanges.value(it.key(), Change::None) != it.value())
            touched.insert(it.key());

    for (const Tag* t : touched)
        if (inTree_.contains(t))
            emitRowChanged(t);
    emit changesUpdated();
}

std::vector<Tag*> NbtModel::rowsOf(const Tag* parent) const
{
    std::vector<Tag*> rows;
    const auto it = deletedRows_.constFind(parent);
    if (it == deletedRows_.constEnd()) {
        rows.reserve(parent->children.size());
        for (const auto& c : parent->children)
            rows.push_back(c.get());
        return rows;
    }
    const auto& ghosts = *it;
    rows.reserve(parent->children.size() + ghosts.size());
    size_t g = 0;
    for (size_t i = 0; i < parent->children.size(); ++i) {
        while (g < ghosts.size() && size_t(ghosts[g].row) <= i)
            rows.push_back(ghosts[g++].copy.get());
        rows.push_back(parent->children[i].get());
    }
    while (g < ghosts.size())
        rows.push_back(ghosts[g++].copy.get());
    return rows;
}

const Tag* NbtModel::parentOf(const Tag* tag) const
{
    return tag->parent ? tag->parent : deletedOwner_.value(tag, nullptr);
}

void NbtModel::addDeletedRow(Tag* parent, int row, const Tag& tag, const Tag* original)
{
    auto& ghosts = deletedRows_[parent];
    int mergedRow = row;
    size_t insertAt = ghosts.size();
    for (size_t i = 0; i < ghosts.size(); ++i) {
        if (ghosts[i].row <= row) {
            ++mergedRow;
        } else {
            insertAt = i;
            break;
        }
    }

    DeletedRow ghost;
    ghost.copy = std::shared_ptr<Tag>(tag.clone());
    ghost.original = original;
    ghost.row = row;
    Tag* copy = ghost.copy.get();

    beginInsertRows(indexFromTag(parent), mergedRow, mergedRow);
    ghosts.insert(ghosts.begin() + ptrdiff_t(insertAt), std::move(ghost));
    deletedOwner_.insert(copy, parent);
    std::vector<const Tag*> stack{copy};
    while (!stack.empty()) {
        const Tag* t = stack.back();
        stack.pop_back();
        deletedTags_.insert(t);
        for (const auto& c : t->children)
            stack.push_back(c.get());
    }
    endInsertRows();
}

bool NbtModel::dropDeletedRow(const Tag* original)
{
    for (auto it = deletedRows_.begin(); it != deletedRows_.end(); ++it) {
        auto& ghosts = it.value();
        for (size_t i = 0; i < ghosts.size(); ++i) {
            if (ghosts[i].original != original)
                continue;
            const Tag* parent = it.key();
            const Tag* copy = ghosts[i].copy.get();
            const auto rows = rowsOf(parent);
            const int mergedRow = int(std::find(rows.begin(), rows.end(), copy) - rows.begin());
            beginRemoveRows(indexFromTag(parent), mergedRow, mergedRow);
            std::vector<const Tag*> stack{copy};
            while (!stack.empty()) {
                const Tag* t = stack.back();
                stack.pop_back();
                deletedTags_.remove(t);
                for (const auto& c : t->children)
                    stack.push_back(c.get());
            }
            deletedOwner_.remove(copy);
            ghosts.erase(ghosts.begin() + ptrdiff_t(i));
            endRemoveRows();
            if (ghosts.empty())
                deletedRows_.erase(it);
            return true;
        }
    }
    return false;
}

void NbtModel::dropDeletedRowsUnder(const Tag* subtree)
{
    std::vector<const Tag*> stack{subtree};
    while (!stack.empty()) {
        const Tag* t = stack.back();
        stack.pop_back();
        if (auto it = deletedRows_.find(t); it != deletedRows_.end()) {
            for (const auto& ghost : it.value()) {
                std::vector<const Tag*> inner{ghost.copy.get()};
                while (!inner.empty()) {
                    const Tag* g = inner.back();
                    inner.pop_back();
                    deletedTags_.remove(g);
                    for (const auto& c : g->children)
                        inner.push_back(c.get());
                }
                deletedOwner_.remove(ghost.copy.get());
            }
            deletedRows_.erase(it);
        }
        for (const auto& c : t->children)
            stack.push_back(c.get());
    }
}

void NbtModel::clearDeletedRows()
{
    while (!deletedRows_.isEmpty()) {
        const auto it = deletedRows_.begin();
        if (it.value().empty()) {
            deletedRows_.erase(it);
            continue;
        }
        dropDeletedRow(it.value().front().original);
    }
    deletedTags_.clear();
    deletedOwner_.clear();
}

const char* NbtModel::mimeType()
{
    return "application/x-justnbt-tags";
}

QByteArray NbtModel::serializeTags(const std::vector<const Tag*>& tags)
{
    std::vector<uint8_t> out;
    const auto count = uint32_t(tags.size());
    for (int i = 3; i >= 0; --i)
        out.push_back(uint8_t(count >> (8 * i)));
    for (const Tag* t : tags)
        nbt::write(*t, nbt::Endian::Big, out);
    return QByteArray(reinterpret_cast<const char*>(out.data()), qsizetype(out.size()));
}

std::vector<std::unique_ptr<Tag>> NbtModel::deserializeTags(const QByteArray& data)
{
    std::vector<std::unique_ptr<Tag>> tags;
    if (data.size() < 4)
        return tags;
    const auto* p = reinterpret_cast<const uint8_t*>(data.constData());
    const uint32_t count = uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
    size_t pos = 4;
    try {
        for (uint32_t i = 0; i < count && pos < size_t(data.size()); ++i) {
            size_t used = 0;
            tags.push_back(nbt::read({p + pos, size_t(data.size()) - pos}, nbt::Endian::Big, &used));
            pos += used;
        }
    } catch (const nbt::ParseError&) {
        tags.clear();
    }
    return tags;
}

namespace {
std::string freeName(const Tag* container, const std::string& wanted)
{
    if (container->type != TagType::Compound || !container->child(wanted))
        return wanted;
    for (int n = 2;; ++n) {
        const std::string candidate = wanted + " " + std::to_string(n);
        if (!container->child(candidate))
            return candidate;
    }
}
}

bool NbtModel::insertTags(Tag* parent, int row, std::vector<std::unique_ptr<Tag>> tags, const QString& what)
{
    if (!parent || !nbt::isContainer(parent->type) || tags.empty())
        return false;
    if (row < 0 || row > int(parent->children.size()))
        row = int(parent->children.size());

    if (parent->type == TagType::List) {
        const TagType wanted = parent->children.empty() ? tags.front()->type : parent->listType;
        for (const auto& t : tags) {
            if (t->type != wanted) {
                emit editRejected(tr("All elements of a list have the same type (%1)")
                                      .arg(QString::fromLatin1(nbt::typeName(wanted))));
                return false;
            }
        }
    }

    undo_->beginMacro(what);
    int at = row;
    for (auto& tag : tags) {
        if (parent->type == TagType::Compound)
            tag->name = freeName(parent, tag->name);
        insertTag(indexFromTag(parent), at++, std::move(tag));
    }
    undo_->endMacro();
    return true;
}

void NbtModel::removeTags(const std::vector<Tag*>& tags)
{
    if (tags.empty())
        return;
    undo_->beginMacro(tags.size() == 1 ? tr("delete %1").arg(displayName(*tags.front()))
                                       : tr("deleting tags: %1").arg(tags.size()));
    for (Tag* t : tags)
        if (t != root_ && t->parent)
            removeTag(indexFromTag(t));
    undo_->endMacro();
}

void NbtModel::duplicateTags(const std::vector<Tag*>& tags)
{
    if (tags.empty())
        return;
    undo_->beginMacro(tags.size() == 1 ? tr("duplicate %1").arg(displayName(*tags.front()))
                                       : tr("duplicating tags: %1").arg(tags.size()));
    for (Tag* t : tags) {
        Tag* parent = t->parent;
        if (!parent)
            continue;
        auto copy = t->clone();
        if (parent->type == TagType::Compound)
            copy->name = freeName(parent, copy->name);
        insertTag(indexFromTag(parent), parent->indexOf(t) + 1, std::move(copy));
    }
    undo_->endMacro();
}

QStringList NbtModel::mimeTypes() const
{
    return {QString::fromLatin1(mimeType())};
}

QMimeData* NbtModel::mimeData(const QModelIndexList& indexes) const
{
    std::vector<const Tag*> tags;
    dragged_.clear();
    for (const QModelIndex& index : indexes) {
        if (index.column() != NameColumn)
            continue;
        Tag* t = tagFromIndex(index);
        if (!t || t == root_ || deletedTags_.contains(t))
            continue;
        tags.push_back(t);
        dragged_.push_back(t);
    }
    if (tags.empty())
        return nullptr;
    auto* data = new QMimeData;
    data->setData(QString::fromLatin1(mimeType()), serializeTags(tags));
    return data;
}

Qt::DropActions NbtModel::supportedDropActions() const
{
    return Qt::CopyAction | Qt::MoveAction;
}

Qt::DropActions NbtModel::supportedDragActions() const
{
    return Qt::CopyAction | Qt::MoveAction;
}

bool NbtModel::canDropMimeData(const QMimeData* data, Qt::DropAction, int, int, const QModelIndex& parent) const
{
    if (!data || !data->hasFormat(QString::fromLatin1(mimeType())))
        return false;
    const Tag* target = tagFromIndex(parent);
    if (!target || deletedTags_.contains(target) || !nbt::isContainer(target->type))
        return false;
    for (const Tag* dragged : dragged_)
        for (const Tag* t = target; t; t = parentOf(t))
            if (t == dragged)
                return false;
    return true;
}

bool NbtModel::dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int, const QModelIndex& parent)
{
    if (!canDropMimeData(data, action, row, 0, parent))
        return false;
    Tag* target = tagFromIndex(parent);
    auto tags = deserializeTags(data->data(QString::fromLatin1(mimeType())));
    if (tags.empty())
        return false;

    std::vector<Tag*> sources;
    if (action == Qt::MoveAction)
        for (Tag* t : dragged_)
            if (t->parent && t != root_)
                sources.push_back(t);

    const QString what = action == Qt::MoveAction ? tr("moving tags: %1").arg(tags.size())
                                                  : tr("pasting tags: %1").arg(tags.size());
    undo_->beginMacro(what);
    int at = row;
    for (Tag* t : sources) {
        if (t->parent == target && at > t->parent->indexOf(t))
            --at;
        removeTag(indexFromTag(t));
    }
    insertTags(target, at, std::move(tags), what);
    undo_->endMacro();
    dragged_.clear();
    return false;
}
void NbtItemDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    QStyledItemDelegate::paint(painter, option, index);
    if (!(option.state & QStyle::State_Selected))
        return;
    const QVariant background = index.data(Qt::BackgroundRole);
    if (!background.canConvert<QColor>())
        return;
    QColor c = background.value<QColor>();
    c.setAlpha(120);
    painter->fillRect(option.rect, c);
}
