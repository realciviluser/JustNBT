#include "ui/MapTab.h"

#include "core/NbtFile.h"
#include "core/WorldBorder.h"
#include "core/WorldEdit.h"
#include "ui/ElidingLabel.h"
#include "ui/MapView.h"
#include "ui/SearchPanel.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QScrollArea>
#include <QScrollBar>
#include <QMenu>
#include <QMessageBox>
#include <QProgressDialog>
#include <QFileInfo>
#include <QSpinBox>
#include <QSplitter>
#include <QToolBar>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

namespace {
const QString kOverworld = QStringLiteral("minecraft:overworld");
const QString kNether = QStringLiteral("minecraft:the_nether");

std::optional<QPointF> readSpawn(const QString& worldPath)
{
    try {
        const auto file = justnbt::NbtFile::load(worldPath + QStringLiteral("/level.dat"));
        const nbt::Tag* data = file->root->child("Data");
        if (!data)
            data = file->root.get();
        const nbt::Tag* x = data->child("SpawnX");
        const nbt::Tag* z = data->child("SpawnZ");
        if (x && z && nbt::isInteger(x->type) && nbt::isInteger(z->type))
            return QPointF(double(x->integer), double(z->integer));
        if (const nbt::Tag* spawn = data->child("spawn")) {
            const nbt::Tag* pos = spawn->child("pos");
            if (pos && pos->type == nbt::TagType::IntArray && pos->ints.size() == 3)
                return QPointF(pos->ints[0], pos->ints[2]);
        }
    } catch (const std::exception&) {
    }
    return std::nullopt;
}
}

MapTab::MapTab(const justnbt::WorldInfo& world, const QString& dimensionId, QWidget* parent)
    : QWidget(parent), world_(world)
{
    spawn_ = readSpawn(world.path);
    view_ = new MapView;

    dimension_ = new QComboBox;
    for (const justnbt::Dimension& d : world_.dimensions)
        dimension_->addItem(d.label, d.id);

    auto* zoomOut = new QToolButton;
    zoomOut->setText(QStringLiteral("−"));
    zoomOut->setToolTip(tr("Zoom out (mouse wheel)"));
    auto* zoomIn = new QToolButton;
    zoomIn->setText(QStringLiteral("+"));
    zoomIn->setToolTip(tr("Zoom in (mouse wheel)"));
    zoomLabel_ = new QLabel;
    zoomLabel_->setMinimumWidth(56);
    zoomLabel_->setAlignment(Qt::AlignCenter);

    border_ = new QCheckBox(tr("Border"));
    border_->setChecked(true);
    border_->setToolTip(tr("The world border as a red line"));
    grid_ = new QCheckBox(tr("Grid"));
    grid_->setToolTip(tr("Region borders, and chunk borders when zoomed in (Tab on the map, or G)"));

    slice_ = new QCheckBox(tr("Y slice:"));
    slice_->setToolTip(tr("Hide the blocks above this height (the Nether roof, caves)"));
    sliceY_ = new QSpinBox;
    sliceY_->setRange(-2048, 2048);
    sliceY_->setValue(100);
    sliceY_->setKeyboardTracking(false);

    select_ = new QCheckBox(tr("Selection"));
    select_->setToolTip(tr("Select chunks with the mouse (a tap on Ctrl switches this too).\nHolding Shift when "
                           "starting adds another area.\nDrag a rectangle by its inside to move it, by an edge or a "
                           "corner to resize it.\nDelete removes the rectangle clicked last (the chunks stay), Esc "
                           "clears them all.\nIn this mode the map is dragged with the middle button."));
    deleteButton_ = new QToolButton;
    deleteButton_->setText(tr("Delete chunks…"));
    deleteButton_->setEnabled(false);
    selectionInfo_ = new ElidingLabel;

    auto* spawn = new QToolButton;
    spawn->setText(tr("To spawn"));
    spawn->setEnabled(spawn_.has_value());
    auto* reload = new QToolButton;
    reload->setText(tr("⟳ Refresh"));
    reload->setToolTip(tr("Read the regions again, changed files are drawn anew (F5)"));
    searchButton_ = new QToolButton;
    searchButton_->setText(tr("Search"));
    searchButton_->setCheckable(true);
    searchButton_->setToolTip(tr("Find blocks, items in chests, entities and points of interest (Ctrl+F)"));
    auto* searchShortcut = new QAction(this);
    searchShortcut->setShortcut(QKeySequence::Find);
    searchShortcut->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(searchShortcut, &QAction::triggered, this, [this] { showSearch(true); });
    addAction(searchShortcut);

    auto* gridShortcut = new QAction(this);
    gridShortcut->setShortcut(QKeySequence(Qt::Key_G));
    gridShortcut->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(gridShortcut, &QAction::triggered, grid_, &QCheckBox::toggle);
    addAction(gridShortcut);

    auto* reloadShortcut = new QAction(this);
    reloadShortcut->setShortcut(QKeySequence::Refresh);
    reloadShortcut->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(reloadShortcut, &QAction::triggered, reload, &QToolButton::click);
    addAction(reloadShortcut);

    auto* toolbar = new QToolBar;
    toolbar->addWidget(new QLabel(tr(" Dimension: ")));
    toolbar->addWidget(dimension_);
    toolbar->addSeparator();
    toolbar->addWidget(zoomOut);
    toolbar->addWidget(zoomLabel_);
    toolbar->addWidget(zoomIn);
    toolbar->addSeparator();
    toolbar->addWidget(grid_);
    toolbar->addWidget(border_);
    toolbar->addSeparator();
    toolbar->addWidget(slice_);
    toolbar->addWidget(sliceY_);
    toolbar->addSeparator();
    toolbar->addWidget(select_);
    toolbar->addWidget(deleteButton_);
    toolbar->addSeparator();
    toolbar->addWidget(spawn);
    toolbar->addWidget(reload);
    toolbar->addSeparator();
    toolbar->addWidget(searchButton_);
    auto* clearMarks = new QToolButton;
    clearMarks->setText(tr("Clear marks"));
    clearMarks->setToolTip(tr("Take away the pin, the found dots and the selected areas"));
    connect(clearMarks, &QToolButton::clicked, this, &MapTab::clearMarks);
    toolbar->addWidget(clearMarks);

    hover_ = new ElidingLabel;
    progress_ = new ElidingLabel;
    borderInfo_ = new ElidingLabel;
    auto* status = new QHBoxLayout;
    status->setContentsMargins(6, 2, 6, 2);
    status->addWidget(selectionInfo_, 3);
    status->addWidget(hover_, 4);
    status->addWidget(borderInfo_, 3);
    status->addWidget(progress_, 3);

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
    search_ = new SearchPanel(world_);
    search_->hide();
    auto* split = new QSplitter(Qt::Horizontal);
    split->addWidget(view_);
    split->addWidget(search_);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 0);
    split->setCollapsible(0, false);
    split->setSizes({900, 380});

    layout->addWidget(toolbarArea);
    layout->addWidget(split, 1);
    layout->addLayout(status);

    connect(searchButton_, &QToolButton::toggled, this, &MapTab::showSearch);
    connect(view_, &MapView::selectionChanged, search_, &SearchPanel::setSelections);
    connect(search_, &SearchPanel::markersChanged, view_, &MapView::setSearchMarkers);
    connect(search_, &SearchPanel::currentHitChanged, this, [this](int index, int x, int z) {
        view_->setCurrentSearchMarker(index);
        view_->centerOn(x + 0.5, z + 0.5);
    });
    connect(search_, &SearchPanel::openChunk, this, [this](int cx, int cz, const QString& folder) {
        if (const justnbt::Dimension* d = currentDimension())
            emit openChunk(*d, cx, cz, folder);
    });

    connect(zoomOut, &QToolButton::clicked, this, [this] { view_->setZoom(view_->zoom() - 1); });
    connect(zoomIn, &QToolButton::clicked, this, [this] { view_->setZoom(view_->zoom() + 1); });
    connect(view_, &MapView::zoomChanged, this, &MapTab::updateZoomLabel);
    connect(grid_, &QCheckBox::toggled, view_, &MapView::setShowGrid);
    connect(view_, &MapView::gridToggleRequested, grid_, &QCheckBox::toggle);
    connect(border_, &QCheckBox::toggled, view_, &MapView::setShowBorder);
    connect(select_, &QCheckBox::toggled, view_, &MapView::setSelectMode);
    connect(view_, &MapView::selectModeChanged, select_, &QCheckBox::setChecked);
    connect(view_, &MapView::selectionChanged, this, &MapTab::selectionChanged);
    connect(deleteButton_, &QToolButton::clicked, this, &MapTab::deleteSelectedChunks);
    connect(slice_, &QCheckBox::toggled, this, &MapTab::applySlice);
    connect(sliceY_, &QSpinBox::valueChanged, this, [this] {
        if (slice_->isChecked())
            applySlice();
    });
    connect(spawn, &QToolButton::clicked, this, &MapTab::goToSpawn);
    connect(reload, &QToolButton::clicked, this, [this] { applyDimension(dimension_->currentIndex(), false); });
    connect(view_, &MapView::hoverInfo, this, [this](const QString& text) { hover_->setText(text); });
    connect(view_, &MapView::progressChanged, this, [this](int ready, int total, int failed) {
        if (total < 0) {
            progress_->setText(tr("Looking for chunks…"));
            return;
        }
        QString text = ready < total ? tr("Drawn %1 of %2 regions").arg(ready).arg(total)
                                     : tr("Regions: %1").arg(total);
        if (failed > 0)
            text += tr("   ·   chunks that could not be read: %1").arg(failed);
        progress_->setText(text);
    });
    connect(dimension_, &QComboBox::currentIndexChanged, this, [this](int index) { applyDimension(index, true); });
    connect(view_, &MapView::chunkMenuRequested, this, &MapTab::showChunkMenu);
    connect(view_, &MapView::chunkActivated, this, [this](int cx, int cz) {
        if (const justnbt::Dimension* d = currentDimension())
            emit openChunk(*d, cx, cz,
                           world_.edition == justnbt::Edition::Bedrock ? QStringLiteral("bedrock") : QStringLiteral("region"));
    });
    connect(view_, &MapView::tilesListed, this, [this] {
        if (centerWhenListed_) {
            centerWhenListed_ = false;
            const QPointF c = view_->regionsCenter();
            view_->centerOn(c.x(), c.y());
        }
        if (view_->regionCount() == 0)
            progress_->setText(tr("This dimension has no saved chunks"));
    });

    updateZoomLabel(view_->zoom());
    const int index = std::max(0, dimension_->findData(dimensionId));
    dimension_->blockSignals(true);
    dimension_->setCurrentIndex(index);
    dimension_->blockSignals(false);
    applyDimension(index, true);
}

const justnbt::Dimension* MapTab::currentDimension() const
{
    const int i = dimension_->currentIndex();
    return i >= 0 && i < world_.dimensions.size() ? &world_.dimensions[i] : nullptr;
}

void MapTab::showChunkMenu(int chunkX, int chunkZ, const QPoint& globalPos)
{
    const justnbt::Dimension* d = currentDimension();
    if (!d)
        return;
    QMenu menu(this);
    menu.addSection(tr("Chunk %1, %2").arg(chunkX).arg(chunkZ));
    auto add = [&](const QString& text, const QString& kind) {
        QAction* a = menu.addAction(text, this, [this, d, chunkX, chunkZ, kind] { emit openChunk(*d, chunkX, chunkZ, kind); });
        if (kind != QLatin1String("bedrock")) {
            const int rx = chunkX >> 5, rz = chunkZ >> 5;
            const bool exists =
                QFileInfo::exists(d->path + QStringLiteral("/%1/r.%2.%3.mca").arg(kind).arg(rx).arg(rz));
            a->setEnabled(exists);
            if (!exists)
                a->setText(text + tr(" — no file"));
        }
    };
    if (world_.edition == justnbt::Edition::Bedrock) {
        add(tr("Chunk: blocks, block entities and entities (NBT)"), QStringLiteral("bedrock"));
    } else {
        add(tr("Chunk: blocks and block entities (NBT)"), QStringLiteral("region"));
        add(tr("Entities of the chunk (NBT)"), QStringLiteral("entities"));
        add(tr("Points of interest, POI (NBT)"), QStringLiteral("poi"));
    }
    menu.exec(globalPos);
}

void MapTab::clearMarks()
{
    view_->setPin(std::nullopt);
    view_->clearSelection();
    search_->clearResults();
}

void MapTab::showPlace(int x, int z)
{
    view_->centerOn(x + 0.5, z + 0.5);
    view_->setPin(QPoint(x, z));
}

void MapTab::showDimension(const QString& dimensionId)
{
    const int index = dimension_->findData(dimensionId);
    if (index >= 0)
        dimension_->setCurrentIndex(index);
}

void MapTab::showSearch(bool show)
{
    const QSignalBlocker block(searchButton_);
    searchButton_->setChecked(show);
    search_->setVisible(show);
    if (show)
        search_->focusQuery();
    else
        view_->setFocus();
}

void MapTab::applyDimension(int index, bool recenter)
{
    if (index < 0 || index >= world_.dimensions.size())
        return;
    const justnbt::Dimension& d = world_.dimensions[index];
    search_->setDimension(d);
    if (recenter) {
        slice_->blockSignals(true);
        slice_->setChecked(d.id == kNether);
        slice_->blockSignals(false);
    }

    justnbt::RenderOptions options;
    if (slice_->isChecked())
        options.yLimit = sliceY_->value();

    std::shared_ptr<justnbt::TileSource> source;
    try {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        source = world_.edition == justnbt::Edition::Bedrock ? justnbt::makeBedrockTileSource(world_.path, d.bedrockId)
                                                        : justnbt::makeJavaTileSource(d.path + QStringLiteral("/region"));
        QApplication::restoreOverrideCursor();
    } catch (const std::exception& e) {
        QApplication::restoreOverrideCursor();
        view_->setSource(nullptr, options);
        progress_->setText(QString::fromUtf8(e.what()));
        return;
    }
    view_->setSource(source, options);

    const bool overworld = d.id == kOverworld;
    view_->setMarker(overworld ? spawn_ : std::nullopt);
    showBorder(d);
    if (!recenter)
        return;
    if (overworld && spawn_) {
        view_->centerOn(spawn_->x(), spawn_->y());
        centerWhenListed_ = false;
    } else {
        centerWhenListed_ = true;
    }
}

void MapTab::showBorder(const justnbt::Dimension& dimension)
{
    const auto b = justnbt::readWorldBorder(world_, dimension);
    border_->setEnabled(b.has_value());
    if (!b) {
        view_->setBorder(std::nullopt);
        borderInfo_->clear();
        return;
    }
    view_->setBorder(QRectF(b->minX(), b->minZ(), b->size, b->size));

    const QLocale locale;
    auto num = [&](double v) {
        return std::abs(v - std::round(v)) < 1e-9 ? locale.toString(qlonglong(std::llround(v)))
                                                  : locale.toString(v, 'f', 1);
    };
    QString text = tr("Border: %1 × %1, centre %2, %3").arg(num(b->size), num(b->centerX), num(b->centerZ));
    if (b->lerpTimeMs > 0 && b->lerpTarget != b->size)
        text += tr(" (moving to %1)").arg(num(b->lerpTarget));
    borderInfo_->setText(text);
    borderInfo_->setToolTip(QDir::toNativeSeparators(b->sourceFile));
}

void MapTab::applySlice()
{
    justnbt::RenderOptions options;
    if (slice_->isChecked())
        options.yLimit = sliceY_->value();
    view_->setRenderOptions(options);
}

void MapTab::goToSpawn()
{
    if (!spawn_)
        return;
    showDimension(kOverworld);
    view_->centerOn(spawn_->x(), spawn_->y());
}

void MapTab::updateZoomLabel(int level)
{
    zoomLabel_->setText(level >= 0 ? QStringLiteral("%1:1").arg(1 << level) : QStringLiteral("1:%1").arg(1 << -level));
}

void MapTab::selectionChanged(const QList<QRect>& areas)
{
    deleteButton_->setEnabled(!areas.isEmpty());
    if (areas.isEmpty()) {
        selectionInfo_->clear();
        return;
    }
    std::vector<justnbt::ChunkRange> ranges;
    for (const QRect& a : areas)
        ranges.push_back({a.left(), a.top(), a.right(), a.bottom()});
    QString text = tr("Selected: %1").arg(tr("%n chunk(s)", nullptr, int(justnbt::chunkCount(ranges))));
    if (areas.size() > 1) {
        text += tr(" in %1").arg(tr("%n area(s)", nullptr, int(areas.size())));
    } else {
        const QRect& a = areas.first();
        text += QStringLiteral(" (%1 × %2, X %3…%4, Z %5…%6)")
                    .arg(a.width())
                    .arg(a.height())
                    .arg(a.left())
                    .arg(a.right())
                    .arg(a.top())
                    .arg(a.bottom());
    }
    selectionInfo_->setText(text);
}

void MapTab::deleteSelectedChunks()
{
    const justnbt::Dimension* d = currentDimension();
    const QList<QRect> areas = view_->selections();
    if (!d || areas.isEmpty())
        return;
    if (justnbt::isWorldInUse(world_)) {
        QMessageBox::warning(this, tr("The world is open in the game"),
                             tr("Close the world in the game or stop the server: while it runs chunks cannot be "
                                "deleted, because the game holds the files and would write them back."));
        return;
    }

    std::vector<justnbt::ChunkRange> ranges;
    for (const QRect& a : areas)
        ranges.push_back({a.left(), a.top(), a.right(), a.bottom()});
    const qint64 count = justnbt::chunkCount(ranges);
    const auto answer = QMessageBox::warning(
        this, tr("Delete chunks"),
        tr("Delete %1 in the dimension %2?\n\nEverything built there is gone. The next time someone goes there the "
           "game generates those chunks again, by the rules of the current version, so the biomes may change where "
           "they meet the old ones.\n\nBackups of the files this touches are made.")
            .arg(tr("%n chunk(s)", nullptr, int(count)), d->label),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Yes)
        return;

    QProgressDialog progress(tr("Deleting chunks…"), tr("Stop"), 0, 100, this);
    progress.setWindowTitle(tr("Deleting chunks"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(300);

    const QString backupSubdir = world_.folderName + tr("/deleted chunks");
    const auto result = justnbt::deleteChunks(world_, *d, ranges, backupSubdir, [&](int done, int total) {
        progress.setMaximum(total);
        progress.setValue(done);
        QCoreApplication::processEvents();
        return !progress.wasCanceled();
    });
    progress.close();

    QString text;
    if (!result.error.isEmpty())
        text = tr("Error: ") + result.error;
    else if (result.cancelled)
        text = tr("Stopped. Deleted so far: %1").arg(tr("%n chunk(s)", nullptr, int(result.chunks)));
    else if (result.chunks == 0)
        text = tr("There were no saved chunks in the selection: nothing to delete.");
    else
        text = tr("Deleted %1.").arg(tr("%n chunk(s)", nullptr, int(result.chunks)));
    if (!result.error.isEmpty())
        QMessageBox::critical(this, tr("Deleting chunks"), text);
    else
        QMessageBox::information(this, tr("Deleting chunks"), text);

    view_->clearSelection();
    applyDimension(dimension_->currentIndex(), false);
}
