#include "ui/SearchPanel.h"

#include "core/GameAssets.h"
#include "core/ItemInfo.h"
#include "ui/ItemVisuals.h"

#include <QAbstractTableModel>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCollator>
#include <QComboBox>
#include <QCompleter>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QProgressBar>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QTableView>
#include <QThread>
#include <QVBoxLayout>

#include <cctype>
#include <tuple>

using justnbt::SearchHit;
using justnbt::SearchKind;

namespace {
constexpr int kMaxHits = 100000;
constexpr int kIdRole = Qt::UserRole + 1;
constexpr int kSortRole = Qt::UserRole + 2;
constexpr int kXRole = Qt::UserRole + 3;
constexpr int kYRole = Qt::UserRole + 4;
constexpr int kZRole = Qt::UserRole + 5;

QString pathOf(const std::string& id)
{
    QString path = QString::fromStdString(id);
    const int colon = path.indexOf(QLatin1Char(':'));
    return colon >= 0 ? path.mid(colon + 1) : path;
}

QString gameName(std::initializer_list<const char*> kinds, const std::string& id)
{
    const QString path = pathOf(id);
    for (const char* kind : kinds) {
        const QString text =
            justnbt::GameAssets::instance().translate(QStringLiteral("%1.minecraft.%2").arg(QLatin1String(kind), path));
        if (!text.isEmpty())
            return text;
    }
    return {};
}

std::string javaBlockEntityId(const std::string& id)
{
    if (id.find(':') != std::string::npos || id.empty() || !std::isupper(uint8_t(id[0])))
        return id;
    std::string snake;
    for (char c : id) {
        if (std::isupper(uint8_t(c))) {
            if (!snake.empty())
                snake += '_';
            snake += char(std::tolower(uint8_t(c)));
        } else {
            snake += c;
        }
    }
    if (snake == "mob_spawner")
        snake = "spawner";
    return "minecraft:" + snake;
}

struct PoiType {
    const char* id;
    const char* block;
    bool work;
};
const PoiType kPoiTypes[] = {
    {"minecraft:home", "minecraft:red_bed", false},        {"minecraft:meeting", "minecraft:bell", false},
    {"minecraft:armorer", "minecraft:blast_furnace", true}, {"minecraft:butcher", "minecraft:smoker", true},
    {"minecraft:cartographer", "minecraft:cartography_table", true},
    {"minecraft:cleric", "minecraft:brewing_stand", true},  {"minecraft:farmer", "minecraft:composter", true},
    {"minecraft:fisherman", "minecraft:barrel", true},      {"minecraft:fletcher", "minecraft:fletching_table", true},
    {"minecraft:leatherworker", "minecraft:cauldron", true}, {"minecraft:librarian", "minecraft:lectern", true},
    {"minecraft:mason", "minecraft:stonecutter", true},     {"minecraft:shepherd", "minecraft:loom", true},
    {"minecraft:toolsmith", "minecraft:smithing_table", true},
    {"minecraft:weaponsmith", "minecraft:grindstone", true}, {"minecraft:beehive", "minecraft:beehive", false},
    {"minecraft:bee_nest", "minecraft:bee_nest", false},    {"minecraft:nether_portal", "minecraft:nether_portal", false},
    {"minecraft:lodestone", "minecraft:lodestone", false},  {"minecraft:lightning_rod", "minecraft:lightning_rod", false},
};

const PoiType* poiType(const std::string& id)
{
    for (const PoiType& t : kPoiTypes)
        if (id == t.id)
            return &t;
    return nullptr;
}

QString poiName(const std::string& id)
{
    const PoiType* t = poiType(id);
    if (!t)
        return justnbt::prettifyId(QString::fromStdString(id));
    if (id == "minecraft:home")
        return SearchPanel::tr("Bed");
    QString name = gameName({"block"}, t->block);
    if (name.isEmpty())
        name = justnbt::prettifyId(QString::fromLatin1(t->block));
    if (t->work) {
        const QString profession = gameName({"entity"}, "villager." + pathOf(id).toStdString());
        if (!profession.isEmpty())
            name += QStringLiteral(" (") + profession + QLatin1Char(')');
    }
    return name;
}

std::string firstWithTexture(std::initializer_list<std::string> ids)
{
    for (const std::string& id : ids)
        if (!justnbt::GameAssets::instance().texturePng(QString::fromStdString(id)).isEmpty())
            return id;
    return *ids.begin();
}

std::string entityIconId(const SearchHit& hit)
{
    if (hit.id == "minecraft:item" && !hit.subject.empty())
        return hit.subject;
    const std::string id = "minecraft:" + pathOf(hit.id).toStdString();
    return firstWithTexture({id + "_spawn_egg", id});
}

std::string iconId(SearchKind kind, const SearchHit& hit)
{
    switch (kind) {
    case SearchKind::Entity: return entityIconId(hit);
    case SearchKind::Poi:
        if (const PoiType* t = poiType(hit.id))
            return t->block;
        return hit.id;
    case SearchKind::Item: {
        const std::string block = javaBlockEntityId(hit.id);
        if (!justnbt::GameAssets::instance().texturePng(QString::fromStdString(block)).isEmpty())
            return block;
        SearchHit holder = hit;
        if (holder.id != "minecraft:item")
            holder.subject.clear();
        return entityIconId(holder);
    }
    case SearchKind::Block: break;
    }
    return hit.id;
}

QString quoted(const QString& name)
{
    return QStringLiteral("«") + name + QStringLiteral("»");
}
}

class SearchResultsModel : public QAbstractTableModel {
public:
    enum Column { NameColumn, PositionColumn, DetailColumn, ColumnCount };

    using QAbstractTableModel::QAbstractTableModel;

    void setHits(std::vector<SearchHit> hits, SearchKind kind)
    {
        beginResetModel();
        hits_ = std::move(hits);
        kind_ = kind;
        names_.assign(hits_.size(), QString());
        details_.assign(hits_.size(), QString());
        endResetModel();
    }
    const std::vector<SearchHit>& hits() const { return hits_; }

    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : int(hits_.size()); }
    int columnCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : ColumnCount; }

    QVariant data(const QModelIndex& index, int role) const override
    {
        if (!index.isValid() || size_t(index.row()) >= hits_.size())
            return {};
        const size_t row = size_t(index.row());
        const SearchHit& h = hits_[row];
        switch (index.column()) {
        case NameColumn:
            if (role == Qt::DisplayRole || role == kSortRole) {
                if (names_[row].isEmpty())
                    names_[row] = SearchPanel::hitName(kind_, h);
                return names_[row];
            }
            if (role == Qt::DecorationRole)
                return justnbt::itemIcon(QString::fromStdString(iconId(kind_, h)));
            if (role == Qt::ToolTipRole)
                return QString::fromStdString(h.id);
            break;
        case PositionColumn:
            if (role == Qt::DisplayRole)
                return QStringLiteral("%1  %2  %3").arg(h.x).arg(h.y).arg(h.z);
            if (role == kXRole)
                return h.x;
            if (role == kYRole)
                return h.y;
            if (role == kZRole)
                return h.z;
            break;
        case DetailColumn:
            if (role == Qt::DisplayRole || role == kSortRole || role == Qt::ToolTipRole) {
                if (details_[row].isEmpty())
                    details_[row] = SearchPanel::hitDetails(kind_, h);
                return details_[row];
            }
            break;
        }
        return {};
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
            return {};
        switch (section) {
        case NameColumn: return SearchPanel::tr("What");
        case PositionColumn: return QStringLiteral("X  Y  Z");
        case DetailColumn: return SearchPanel::tr("Details");
        }
        return {};
    }

private:
    std::vector<SearchHit> hits_;
    SearchKind kind_ = SearchKind::Block;
    mutable std::vector<QString> names_;
    mutable std::vector<QString> details_;
};

namespace {
class IdCompleter : public QCompleter {
public:
    using QCompleter::QCompleter;
    QString pathFromIndex(const QModelIndex& index) const override { return index.data(kIdRole).toString(); }
};

class ResultsSorter : public QSortFilterProxyModel {
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

protected:
    bool lessThan(const QModelIndex& a, const QModelIndex& b) const override
    {
        if (a.column() == SearchResultsModel::PositionColumn) {
            const auto key = [](const QModelIndex& i) {
                return std::tuple(i.data(kXRole).toInt(), i.data(kZRole).toInt(), i.data(kYRole).toInt());
            };
            return key(a) < key(b);
        }
        const QVariant l = a.data(kSortRole), r = b.data(kSortRole);
        static const QCollator collator = [] {
            QCollator c;
            c.setNumericMode(true);
            c.setCaseSensitivity(Qt::CaseInsensitive);
            return c;
        }();
        return collator.compare(l.toString(), r.toString()) < 0;
    }
};
}

SearchPanel::SearchPanel(const justnbt::WorldInfo& world, QWidget* parent) : QWidget(parent), world_(world)
{
    kindBox_ = new QComboBox;
    kindBox_->addItem(tr("Blocks"), int(SearchKind::Block));
    kindBox_->addItem(tr("Items in chests and entities"), int(SearchKind::Item));
    kindBox_->addItem(tr("Entities"), int(SearchKind::Entity));
    if (justnbt::searchSupported(world_, SearchKind::Poi))
        kindBox_->addItem(tr("Points of interest"), int(SearchKind::Poi));
    kindBox_->setToolTip(tr("Points of interest are beds, workstations, bells, bee nests and portals that "
                            "villagers and bees remember (Java only)."));

    query_ = new QLineEdit;
    query_->setClearButtonEnabled(true);
    query_->setToolTip(tr("A name as the game shows it, or an id. Part of a name finds every block that has it: "
                          "\"diamond_ore\" also finds deepslate diamond ore. Several things: separate them with "
                          "commas."));
    auto* completer = new IdCompleter(this);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setFilterMode(Qt::MatchContains);
    completer->setMaxVisibleItems(14);
    completer->setModel(new QStandardItemModel(completer));
    query_->setCompleter(completer);

    selectedOnly_ = new QCheckBox(tr("Only in the selected area"));
    selectedOnly_->setToolTip(tr("Look only inside the rectangles selected on the map - much faster than the whole "
                                 "dimension."));
    selectedOnly_->setEnabled(false);

    button_ = new QPushButton(tr("Search"));
    button_->setDefault(true);
    progress_ = new QProgressBar;
    progress_->setTextVisible(false);
    progress_->setMaximumHeight(8);
    progress_->hide();
    status_ = new QLabel;
    status_->setWordWrap(true);

    model_ = new SearchResultsModel(this);
    sorted_ = new ResultsSorter(this);
    sorted_->setSourceModel(model_);
    sorted_->setSortRole(kSortRole);
    table_ = new QTableView;
    table_->setModel(sorted_);
    table_->setSortingEnabled(true);
    table_->sortByColumn(-1, Qt::AscendingOrder);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setAlternatingRowColors(true);
    table_->setContextMenuPolicy(Qt::CustomContextMenu);
    table_->verticalHeader()->hide();
    table_->verticalHeader()->setDefaultSectionSize(table_->fontMetrics().height() + 6);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->horizontalHeader()->setSectionResizeMode(SearchResultsModel::NameColumn, QHeaderView::Interactive);
    table_->setColumnWidth(SearchResultsModel::NameColumn, 150);
    table_->setColumnWidth(SearchResultsModel::PositionColumn,
                           table_->fontMetrics().horizontalAdvance(QStringLiteral("-30000000  -64  -30000000")) / 2 + 24);
    table_->setIconSize(QSize(16, 16));

    auto* row = new QHBoxLayout;
    row->addWidget(query_, 1);
    row->addWidget(button_);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->addWidget(kindBox_);
    layout->addLayout(row);
    layout->addWidget(selectedOnly_);
    layout->addWidget(progress_);
    layout->addWidget(status_);
    layout->addWidget(table_, 1);

    connect(kindBox_, &QComboBox::currentIndexChanged, this, &SearchPanel::kindChanged);
    connect(button_, &QPushButton::clicked, this, &SearchPanel::startOrStop);
    connect(query_, &QLineEdit::returnPressed, this, [this] {
        if (!isSearching())
            startSearch();
    });
    connect(table_->selectionModel(), &QItemSelectionModel::currentRowChanged, this, [this](const QModelIndex& current) {
        const int i = hitIndex(current);
        if (i >= 0)
            emit currentHitChanged(i, model_->hits()[size_t(i)].x, model_->hits()[size_t(i)].z);
    });
    connect(table_, &QTableView::doubleClicked, this, [this](const QModelIndex& index) {
        const int i = hitIndex(index);
        if (i < 0)
            return;
        const SearchHit& h = model_->hits()[size_t(i)];
        emit openChunk(h.x >> 4, h.z >> 4, chunkFolder(h));
    });
    connect(table_, &QTableView::customContextMenuRequested, this, &SearchPanel::showMenu);
    kindChanged();
}

SearchPanel::~SearchPanel()
{
    if (cancel_)
        *cancel_ = true;
    if (thread_)
        thread_->wait();
}

void SearchPanel::setDimension(const justnbt::Dimension& dimension)
{
    if (dimension.id == dimension_.id && dimension.path == dimension_.path)
        return;
    dimension_ = dimension;
    if (cancel_)
        *cancel_ = true;
    ++searchId_;
    model_->setHits({}, kind_);
    status_->clear();
    emit markersChanged({});
}

void SearchPanel::clearResults()
{
    if (cancel_)
        *cancel_ = true;
    ++searchId_;
    model_->setHits({}, kind_);
    status_->clear();
    emit markersChanged({});
}

void SearchPanel::setSelections(const QList<QRect>& areas)
{
    selections_ = areas;
    selectedOnly_->setEnabled(!areas.isEmpty() && !isSearching());
    if (areas.isEmpty())
        selectedOnly_->setChecked(false);
}

void SearchPanel::search(SearchKind kind, const QString& text, bool selectedAreaOnly)
{
    kindBox_->setCurrentIndex(std::max(0, kindBox_->findData(int(kind))));
    query_->setText(text);
    selectedOnly_->setChecked(selectedAreaOnly && !selections_.isEmpty());
    startSearch();
}

const std::vector<SearchHit>& SearchPanel::hits() const
{
    return model_->hits();
}

void SearchPanel::focusQuery()
{
    query_->setFocus();
    query_->selectAll();
}

void SearchPanel::kindChanged()
{
    const auto kind = SearchKind(kindBox_->currentData().toInt());
    const justnbt::GameAssets& assets = justnbt::GameAssets::instance();
    names_.clear();
    switch (kind) {
    case SearchKind::Block: names_ = assets.namedIds(QStringLiteral("block")); break;
    case SearchKind::Item: names_ = assets.namedIds(QStringLiteral("item")) + assets.namedIds(QStringLiteral("block")); break;
    case SearchKind::Entity: names_ = assets.namedIds(QStringLiteral("entity")); break;
    case SearchKind::Poi:
        for (const PoiType& t : kPoiTypes)
            names_.append({QString::fromLatin1(t.id), poiName(t.id)});
        break;
    }
    QStringList seen;
    auto* suggestions = static_cast<QStandardItemModel*>(query_->completer()->model());
    suggestions->clear();
    QList<QPair<QString, QString>> sortedNames = names_;
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(sortedNames.begin(), sortedNames.end(),
              [&](const auto& a, const auto& b) { return collator.compare(a.second, b.second) < 0; });
    for (const auto& [id, name] : sortedNames) {
        if (seen.contains(id))
            continue;
        seen << id;
        auto* item = new QStandardItem(name + QStringLiteral(" — ") + id);
        item->setData(id, kIdRole);
        suggestions->appendRow(item);
    }

    static const char* const examples[] = {
        QT_TR_NOOP("For example: diamond ore, spawner, ancient_debris"),
        QT_TR_NOOP("For example: diamond, elytra, netherite_ingot"),
        QT_TR_NOOP("For example: villager, allay, armor_stand"),
        QT_TR_NOOP("For example: bed, librarian, nether_portal"),
    };
    query_->setPlaceholderText(tr(examples[int(kind)]));
}

QStringList SearchPanel::patternsFor(const QString& text) const
{
    QStringList patterns;
    for (QString term : text.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        term = term.trimmed();
        if (const int dash = term.lastIndexOf(QStringLiteral(" — ")); dash >= 0)
            term = term.mid(dash + 3).trimmed();
        if (term.isEmpty())
            continue;
        if (term.contains(QLatin1Char(':'))) {
            patterns << term.toLower();
            continue;
        }
        patterns << QString(term).replace(QLatin1Char(' '), QLatin1Char('_')).toLower();
        QStringList byName;
        for (const auto& [id, name] : names_)
            if (name.contains(term, Qt::CaseInsensitive))
                byName << id;
        if (byName.size() <= 300)
            patterns << byName;
    }
    patterns.removeDuplicates();
    return patterns;
}

void SearchPanel::startOrStop()
{
    if (isSearching()) {
        if (cancel_)
            *cancel_ = true;
        button_->setEnabled(false);
        return;
    }
    startSearch();
}

void SearchPanel::setBusy(bool busy)
{
    button_->setText(busy ? tr("Stop") : tr("Search"));
    button_->setEnabled(true);
    kindBox_->setEnabled(!busy);
    query_->setEnabled(!busy);
    selectedOnly_->setEnabled(!busy && !selections_.isEmpty());
    progress_->setVisible(busy);
}

void SearchPanel::startSearch()
{
    if (isSearching() || dimension_.id.isEmpty())
        return;
    justnbt::SearchQuery query;
    query.kind = SearchKind(kindBox_->currentData().toInt());
    query.patterns = patternsFor(query_->text());
    query.maxHits = kMaxHits;
    if (query.patterns.isEmpty()) {
        status_->setText(tr("Type what to look for."));
        return;
    }
    if (selectedOnly_->isChecked())
        for (const QRect& r : std::as_const(selections_))
            query.areas.push_back({r.left(), r.top(), r.right(), r.bottom()});

    kind_ = query.kind;
    model_->setHits({}, kind_);
    emit markersChanged({});
    status_->setText(tr("Searching…"));
    progress_->setRange(0, 0);
    setBusy(true);

    cancel_ = std::make_shared<std::atomic<bool>>(false);
    const auto cancel = cancel_;
    const int id = ++searchId_;
    const justnbt::WorldInfo world = world_;
    const justnbt::Dimension dimension = dimension_;
    QPointer<SearchPanel> self(this);
    thread_ = QThread::create([self, cancel, world, dimension, query, id] {
        auto result = std::make_shared<justnbt::SearchResult>(
            justnbt::searchWorld(world, dimension, query, [self, cancel](int done, int total) {
                if (self)
                    QMetaObject::invokeMethod(self, [self, done, total] {
                        if (!self)
                            return;
                        self->progress_->setRange(0, std::max(1, total));
                        self->progress_->setValue(done);
                    }, Qt::QueuedConnection);
                return !*cancel;
            }));
        if (self)
            QMetaObject::invokeMethod(self, [self, result, id] {
                if (self)
                    self->finish(result, id);
            }, Qt::QueuedConnection);
    });
    connect(thread_, &QThread::finished, thread_, &QObject::deleteLater);
    thread_->start();
}

void SearchPanel::finish(std::shared_ptr<justnbt::SearchResult> result, int searchId)
{
    if (thread_)
        thread_->wait();
    thread_ = nullptr;
    setBusy(false);
    if (searchId != searchId_) {
        status_->clear();
        return;
    }
    if (!result->error.isEmpty()) {
        status_->setText(result->error);
        emit searchFinished();
        return;
    }
    const int found = int(result->hits.size());
    std::vector<QPoint> markers;
    markers.reserve(result->hits.size());
    for (const SearchHit& h : result->hits)
        markers.emplace_back(h.x, h.z);
    model_->setHits(std::move(result->hits), kind_);

    QString text = result->cancelled ? tr("Stopped. Found so far: %1").arg(found)
                   : result->truncated ? tr("Found %1 - the search stopped there. Select a smaller area or look "
                                            "for something rarer.")
                                             .arg(found)
                                       : found == 0 ? tr("Nothing found.") : tr("Found: %1").arg(found);
    text += QLatin1Char('\n') + tr("Chunks looked at: %1").arg(result->chunks);
    if (result->failedChunks > 0)
        text += QStringLiteral(" · ") + tr("could not be read: %1").arg(result->failedChunks);
    if (result->legacyChunks > 0)
        text += QStringLiteral(" · ") + tr("too old to search for blocks: %1").arg(result->legacyChunks);
    status_->setText(text);
    emit markersChanged(markers);
    emit searchFinished();
}

int SearchPanel::hitIndex(const QModelIndex& viewIndex) const
{
    if (!viewIndex.isValid())
        return -1;
    return sorted_->mapToSource(viewIndex).row();
}

void SearchPanel::showMenu(const QPoint& pos)
{
    const QModelIndex index = table_->indexAt(pos);
    const int i = hitIndex(index);
    if (i < 0)
        return;
    table_->setCurrentIndex(index);
    const SearchHit h = model_->hits()[size_t(i)];
    QMenu menu(this);
    menu.addAction(tr("Show on the map"), this, [this, i, h] { emit currentHitChanged(i, h.x, h.z); });
    menu.addAction(tr("Open the chunk as NBT"), this, [this, h] { emit openChunk(h.x >> 4, h.z >> 4, chunkFolder(h)); });
    menu.addAction(tr("Copy the coordinates"), this, [h] {
        QApplication::clipboard()->setText(QStringLiteral("%1 %2 %3").arg(h.x).arg(h.y).arg(h.z));
    });
    menu.exec(table_->viewport()->mapToGlobal(pos));
}

QString SearchPanel::chunkFolder(const SearchHit& hit)
{
    switch (hit.source) {
    case justnbt::SearchSource::Entities: return QStringLiteral("entities");
    case justnbt::SearchSource::Poi: return QStringLiteral("poi");
    case justnbt::SearchSource::Bedrock: return QStringLiteral("bedrock");
    case justnbt::SearchSource::Region: break;
    }
    return QStringLiteral("region");
}

QString SearchPanel::hitName(SearchKind kind, const SearchHit& hit)
{
    QString name;
    switch (kind) {
    case SearchKind::Block: name = gameName({"block", "item"}, hit.id); break;
    case SearchKind::Entity: name = gameName({"entity"}, hit.id); break;
    case SearchKind::Poi: return poiName(hit.id);
    case SearchKind::Item:
        name = gameName({"block", "entity", "item"}, javaBlockEntityId(hit.id));
        break;
    }
    return name.isEmpty() ? justnbt::prettifyId(QString::fromStdString(javaBlockEntityId(hit.id))) : name;
}

QString SearchPanel::hitDetails(SearchKind kind, const SearchHit& hit)
{
    QStringList parts;
    switch (kind) {
    case SearchKind::Block:
        if (!hit.subject.empty()) {
            QString mob = gameName({"entity"}, hit.subject);
            parts << tr("Spawner: %1").arg(mob.isEmpty() ? justnbt::prettifyId(QString::fromStdString(hit.subject)) : mob);
        }
        break;
    case SearchKind::Item: {
        QString item = gameName({"item", "block"}, hit.subject);
        if (item.isEmpty())
            item = justnbt::prettifyId(QString::fromStdString(hit.subject));
        parts << QStringLiteral("%1 × %2").arg(item).arg(hit.count);
        break;
    }
    case SearchKind::Entity:
        if (!hit.subject.empty()) {
            QString what = gameName({"entity"}, "villager." + pathOf(hit.subject).toStdString());
            if (what.isEmpty())
                what = gameName({"item", "block"}, hit.subject);
            if (what.isEmpty())
                what = justnbt::prettifyId(QString::fromStdString(hit.subject));
            parts << what;
        }
        break;
    case SearchKind::Poi:
        if (const PoiType* t = poiType(hit.id); t && (t->work || hit.id == "minecraft:home"))
            parts << (hit.count > 0 ? tr("free") : tr("claimed"));
        break;
    }
    if (!hit.name.isEmpty())
        parts << quoted(hit.name);
    return parts.join(QStringLiteral(" · "));
}
