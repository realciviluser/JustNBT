#include "ui/MainWindow.h"

#include "core/Backup.h"
#include "core/Config.h"
#include "core/GameAssets.h"
#include "core/NbtFile.h"
#include "ui/InventoryPanel.h"
#include "ui/MaintenanceDialog.h"
#include "ui/MinecraftSourceDialog.h"
#include "ui/MapTab.h"
#include "ui/FileAssociation.h"
#include "ui/ItemVisuals.h"
#include "ui/NbtEditor.h"

#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QProcess>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QUndoGroup>
#include <QUndoStack>
#include <QUrl>
#include <QVBoxLayout>

using justnbt::Edition;
using justnbt::WorldInfo;

namespace {
constexpr int kWorldIndexRole = Qt::UserRole;
constexpr int kFilePathRole = Qt::UserRole + 1;
constexpr int kDbKeyRole = Qt::UserRole + 2;
constexpr int kMapDimensionRole = Qt::UserRole + 3;
constexpr int kWorldSourceRole = Qt::UserRole + 4;
const QString kExtraRootsKey = QStringLiteral("worlds/extraRoots");
const QString kManualWorldsKey = QStringLiteral("worlds/manual");
const QString kAutoScanKey = QStringLiteral("worlds/autoScan");
const QString kRestoreTabsKey = QStringLiteral("session/restoreTabs");
const QString kSessionArray = QStringLiteral("session/tabs");

QIcon worldIcon(const WorldInfo& w, const QIcon& fallback)
{
    if (!w.iconPath.isEmpty()) {
        QPixmap pm(w.iconPath);
        if (!pm.isNull())
            return QIcon(pm.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    return fallback;
}

QTreeWidgetItem* makeGroup(QTreeWidget* tree, const QString& text)
{
    auto* item = new QTreeWidgetItem(tree, {text});
    QFont f = item->font(0);
    f.setBold(true);
    item->setFont(0, f);
    item->setFlags(Qt::ItemIsEnabled);
    item->setExpanded(true);
    return item;
}
}

MainWindow::MainWindow()
{
    setWindowTitle(QStringLiteral("JustNBT"));
    setAcceptDrops(true);
    resize(1280, 800);

    worldsTree_ = new QTreeWidget;
    worldsTree_->setObjectName(QStringLiteral("worlds"));
    worldsTree_->setHeaderHidden(true);
    worldsTree_->setIconSize(QSize(32, 32));
    worldsTree_->setRootIsDecorated(false);

    worldTitle_ = new QLabel(tr("Choose a world"));
    worldTitle_->setWordWrap(true);
    worldTitle_->setContentsMargins(4, 4, 4, 0);
    QFont titleFont = worldTitle_->font();
    titleFont.setBold(true);
    worldTitle_->setFont(titleFont);

    lockWarning_ = new QLabel(tr("⚠ The world is open in the game right now. Close it before saving."));
    lockWarning_->setWordWrap(true);
    lockWarning_->setStyleSheet(QStringLiteral("color: #c0392b; padding: 4px;"));
    lockWarning_->hide();

    filesTree_ = new QTreeWidget;
    filesTree_->setObjectName(QStringLiteral("files"));
    filesTree_->setHeaderHidden(true);

    filesFilter_ = new QLineEdit;
    filesFilter_->setPlaceholderText(tr("Filter: name, UUID, file name"));
    filesFilter_->setClearButtonEnabled(true);
    connect(filesFilter_, &QLineEdit::textChanged, this, &MainWindow::filterFiles);

    auto* refreshFiles = new QToolButton;
    refreshFiles->setText(QStringLiteral("⟳"));
    refreshFiles->setToolTip(tr("Read the file list of this world again"));
    connect(refreshFiles, &QToolButton::clicked, this, [this] {
        if (currentWorld_ >= 0)
            showWorld(currentWorld_);
    });
    auto* filterRow = new QHBoxLayout;
    filterRow->setContentsMargins(0, 0, 0, 0);
    filterRow->addWidget(filesFilter_, 1);
    filterRow->addWidget(refreshFiles);

    auto* filesPanel = new QWidget;
    auto* filesLayout = new QVBoxLayout(filesPanel);
    filesLayout->setContentsMargins(0, 0, 0, 0);
    filesLayout->addWidget(worldTitle_);
    filesLayout->addWidget(lockWarning_);
    filesLayout->addLayout(filterRow);
    filesLayout->addWidget(filesTree_);

    auto* left = new QSplitter(Qt::Vertical);
    left->addWidget(worldsTree_);
    left->addWidget(filesPanel);
    left->setStretchFactor(0, 3);
    left->setStretchFactor(1, 2);

    tabs_ = new QTabWidget;
    tabs_->setTabsClosable(true);
    tabs_->setMovable(true);
    tabs_->setDocumentMode(true);

    auto* hint = new QLabel(tr("Pick a world on the left and double-click a file,\nor open any NBT file from the "
                               "File menu."));
    hint->setAlignment(Qt::AlignCenter);
    QPalette pal = hint->palette();
    pal.setColor(QPalette::WindowText, pal.color(QPalette::PlaceholderText));
    hint->setPalette(pal);

    stack_ = new QStackedWidget;
    stack_->addWidget(hint);
    stack_->addWidget(tabs_);

    auto* main = new QSplitter(Qt::Horizontal);
    main->addWidget(left);
    main->addWidget(stack_);
    main->setStretchFactor(0, 0);
    main->setStretchFactor(1, 1);
    main->setSizes({320, 960});
    setCentralWidget(main);

    undoGroup_ = new QUndoGroup(this);
    buildMenus();

    connect(worldsTree_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) {
        if (item && item->data(0, kWorldIndexRole).isValid())
            showWorld(item->data(0, kWorldIndexRole).toInt());
    });
    connect(filesTree_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item) {
        if (currentWorld_ < 0)
            return;
        const WorldInfo& w = worlds_[currentWorld_];
        if (const QVariant dim = item->data(0, kMapDimensionRole); dim.isValid()) {
            openMap(w, dim.toString());
            return;
        }
        const QString path = item->data(0, kFilePathRole).toString();
        if (path.isEmpty())
            return;
        openEditor(path, item->data(0, kDbKeyRole).toByteArray(), w.name + QStringLiteral(" — ") + item->text(0), &w);
    });
    connect(tabs_, &QTabWidget::tabCloseRequested, this, &MainWindow::closeTab);
    connect(tabs_, &QTabWidget::currentChanged, this, [this](int index) {
        NbtEditor* editor = editorAt(index);
        undoGroup_->setActiveStack(editor ? editor->undoStack() : nullptr);
    });

    worldsTree_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(worldsTree_, &QTreeWidget::customContextMenuRequested, this, &MainWindow::showWorldMenu);

    restoreGeometry(justnbt::settings().value(QStringLiteral("window/geometry")).toByteArray());

    refreshWorlds();
    statusBar()->showMessage(tr("Worlds found: %1").arg(worlds_.size()), 5000);

    QTimer::singleShot(0, this, &MainWindow::restoreSession);
}

void MainWindow::buildMenus()
{
    QMenu* file = menuBar()->addMenu(tr("&File"));
    file->addAction(tr("Open world (folder)…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O), this,
                    &MainWindow::openWorldFolder);
    file->addAction(tr("Open NBT file…"), QKeySequence::Open, this, &MainWindow::openNbtFile);
    file->addSeparator();
    file->addAction(tr("Add a folder with worlds…"), this, &MainWindow::addWorldsRoot);
    QAction* autoScan = file->addAction(tr("Look for Minecraft worlds automatically"));
    autoScan->setCheckable(true);
    autoScan->setChecked(justnbt::settings().value(kAutoScanKey, true).toBool());
    autoScan->setToolTip(tr("When this is off, only the worlds and folders you added stay in the list"));
    connect(autoScan, &QAction::toggled, this, [this](bool on) {
        justnbt::settings().setValue(kAutoScanKey, on);
        refreshWorlds();
    });
    QAction* restoreTabs = file->addAction(tr("Reopen the last tabs at start"));
    restoreTabs->setCheckable(true);
    restoreTabs->setChecked(justnbt::settings().value(kRestoreTabsKey, true).toBool());
    restoreTabs->setToolTip(tr("The tabs that were open at exit are opened again. Files that are gone are skipped."));
    connect(restoreTabs, &QAction::toggled, this,
            [](bool on) { justnbt::settings().setValue(kRestoreTabsKey, on); });

    if (justnbt::fileAssociationSupported()) {
        QAction* openWith = file->addAction(tr("Offer JustNBT in \"Open with\" for NBT files"));
        openWith->setCheckable(true);
        openWith->setToolTip(tr("For your account only, no administrator rights needed. The program that "
                                "opens .dat files by default stays the same."));
        auto refresh = [openWith] {
            const QSignalBlocker block(openWith);
            openWith->setChecked(justnbt::isFileAssociationSet(QApplication::applicationFilePath()));
        };
        refresh();
        connect(file, &QMenu::aboutToShow, this, refresh);
        connect(openWith, &QAction::toggled, this, [this, refresh](bool on) {
            QString error;
            if (!justnbt::setFileAssociation(on, QApplication::applicationFilePath(), &error))
                QMessageBox::warning(this, tr("Open with"), error);
            else
                statusBar()->showMessage(on ? tr("JustNBT is now in \"Open with\" for NBT files")
                                            : tr("JustNBT was taken out of \"Open with\""),
                                         6000);
            refresh();
        });
    }
    file->addAction(tr("Refresh the world list"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R), this,
                    &MainWindow::refreshWorlds);
    file->addSeparator();
    file->addAction(tr("Quit"), QKeySequence::Quit, this, &QWidget::close);

    QAction* undo = undoGroup_->createUndoAction(this, tr("Undo"));
    undo->setShortcuts(QKeySequence::Undo);
    QAction* redo = undoGroup_->createRedoAction(this, tr("Redo"));
    redo->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_Y), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z)});
    addAction(undo);
    addAction(redo);

    file->addSeparator();
    file->addAction(tr("Minecraft for item names and pictures…"), this, &MainWindow::chooseMinecraftDir);
    QMenu* languages = file->addMenu(tr("Language"));
    auto* languageGroup = new QActionGroup(this);
    const QString configured = justnbt::configuredLanguage();
    for (const auto& [code, title] : {std::pair<const char*, QString>{"auto", tr("Automatic")},
                                      {"en", QStringLiteral("English")},
                                      {"ru", QStringLiteral("Русский")}}) {
        QAction* action = languages->addAction(title);
        action->setCheckable(true);
        action->setChecked(configured == QLatin1String(code));
        languageGroup->addAction(action);
        const QString language = QString::fromLatin1(code);
        connect(action, &QAction::triggered, this, [this, language] { chooseLanguage(language); });
    }

    QMenu* help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("Backups and cache…"), this, [this] {
        MaintenanceDialog dialog(this);
        dialog.exec();
    });
    help->addAction(tr("About"), this, [this] {
        QMessageBox::about(this, tr("About"),
                           tr("<b>JustNBT</b> %1<br>An editor for Minecraft worlds (Java and Bedrock).<br><br>A "
                              "backup of the file is made before every save.<br><br>%2")
                               .arg(QApplication::applicationVersion(), justnbt::GameAssets::instance().describeSource()));
    });
}

void MainWindow::refreshWorlds()
{
    QString selectedPath = currentWorld_ >= 0 ? worlds_[currentWorld_].path : QString();
    worlds_.clear();
    currentWorld_ = -1;
    worldsTree_->clear();

    const QIcon folderIcon = style()->standardIcon(QStyle::SP_DirIcon);
    QHash<QString, QString> sourceOfWorld;
    auto addGroup = [&](const QString& title, const QList<WorldInfo>& list) {
        if (list.isEmpty())
            return;
        QTreeWidgetItem* group = makeGroup(worldsTree_, QStringLiteral("%1  (%2)").arg(title).arg(list.size()));
        for (const WorldInfo& w : list) {
            worlds_ << w;
            auto* item = new QTreeWidgetItem(group, {w.name});
            item->setIcon(0, worldIcon(w, folderIcon));
            item->setData(0, kWorldIndexRole, int(worlds_.size() - 1));
            item->setData(0, kWorldSourceRole, sourceOfWorld.value(w.path, QStringLiteral("manual")));
            item->setToolTip(0, tr("%1\nLast played: %2")
                                    .arg(QDir::toNativeSeparators(w.path),
                                         QLocale().toString(w.lastPlayed, QLocale::ShortFormat)));
            if (w.path == selectedPath)
                worldsTree_->setCurrentItem(item);
        }
    };

    manualWorlds_.clear();
    QStringList manualPaths = justnbt::settings().value(kManualWorldsKey).toStringList();
    QStringList stillThere;
    for (const QString& path : manualPaths) {
        if (auto w = justnbt::probeWorld(path)) {
            manualWorlds_ << *w;
            stillThere << path;
        }
    }
    if (stillThere != manualPaths)
        justnbt::settings().setValue(kManualWorldsKey, stillThere);

    QList<WorldInfo> java, bedrock;
    QStringList roots = justnbt::settings().value(kExtraRootsKey).toStringList();
    if (justnbt::settings().value(kAutoScanKey, true).toBool())
        roots = justnbt::defaultJavaRoots() + justnbt::defaultBedrockRoots() + roots;
    roots.removeDuplicates();
    for (const QString& root : roots) {
        for (const WorldInfo& w : justnbt::scanRoot(root)) {
            sourceOfWorld.insert(w.path, root);
            (w.edition == Edition::Java ? java : bedrock) << w;
        }
    }
    addGroup(QStringLiteral("Java Edition"), java);
    addGroup(QStringLiteral("Bedrock Edition"), bedrock);
    addGroup(tr("Opened by hand"), manualWorlds_);

    if (worlds_.isEmpty()) {
        auto* item = new QTreeWidgetItem(worldsTree_, {tr("No worlds found — File → Open world")});
        item->setFlags(Qt::ItemIsEnabled);
    }
}

void MainWindow::showWorld(int worldIndex)
{
    currentWorld_ = worldIndex;
    const WorldInfo& w = worlds_[worldIndex];
    worldTitle_->setText(QStringLiteral("%1   ·   %2").arg(w.name, w.description()));
    QString tip = QDir::toNativeSeparators(w.path);
    for (const justnbt::Dimension& d : w.dimensions)
        tip += QStringLiteral("\n%1: %2").arg(d.label, QDir::toNativeSeparators(d.path));
    worldTitle_->setToolTip(tip);
    lockWarning_->setVisible(justnbt::isWorldInUse(w));

    filesFilter_->clear();
    filesTree_->clear();

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const QList<justnbt::WorldFile> files = justnbt::editableFiles(w);
    QApplication::restoreOverrideCursor();

    {
        QTreeWidgetItem* mapGroup = nullptr;
        for (const justnbt::Dimension& d : w.dimensions) {
            if (w.edition == Edition::Java && !QFileInfo(d.path + QStringLiteral("/region")).isDir())
                continue;
            if (!mapGroup)
                mapGroup = makeGroup(filesTree_, tr("Map"));
            auto* item = new QTreeWidgetItem(mapGroup, {d.label});
            item->setIcon(0, style()->standardIcon(QStyle::SP_DirIcon));
            item->setData(0, kMapDimensionRole, d.id);
            item->setToolTip(0, tr("Open the 2D map: ") + d.id);
        }
    }

    QHash<QString, QTreeWidgetItem*> groups;
    const QIcon fileIcon = style()->standardIcon(QStyle::SP_FileIcon);
    for (const justnbt::WorldFile& f : files) {
        QTreeWidgetItem*& group = groups[f.group];
        if (!group)
            group = makeGroup(filesTree_, f.group);
        auto* item = new QTreeWidgetItem(group, {f.label});
        if (f.path.isEmpty()) {
            item->setFlags(Qt::ItemIsEnabled);
            item->setToolTip(0, f.label);
            continue;
        }
        item->setIcon(0, fileIcon);
        item->setData(0, kFilePathRole, f.path);
        item->setData(0, kDbKeyRole, f.dbKey);
        item->setToolTip(0, f.dbKey.isEmpty() ? QDir::toNativeSeparators(f.path)
                                              : tr("Database record: ") + justnbt::keyToText(f.dbKey));
    }
    for (auto* group : std::as_const(groups)) {
        int count = 0;
        for (int i = 0; i < group->childCount(); ++i)
            count += group->child(i)->data(0, kFilePathRole).isValid() ? 1 : 0;
        if (count > 0)
            group->setText(0, QStringLiteral("%1  (%2)").arg(group->text(0)).arg(count));
        group->setExpanded(group->childCount() <= 50);
    }
}

void MainWindow::filterFiles(const QString& text)
{
    const QString query = text.trimmed();
    for (int g = 0; g < filesTree_->topLevelItemCount(); ++g) {
        QTreeWidgetItem* group = filesTree_->topLevelItem(g);
        int visible = 0;
        for (int i = 0; i < group->childCount(); ++i) {
            QTreeWidgetItem* item = group->child(i);
            const bool match = query.isEmpty() || item->text(0).contains(query, Qt::CaseInsensitive);
            item->setHidden(!match);
            visible += match ? 1 : 0;
        }
        group->setHidden(group->childCount() > 0 && visible == 0);
        if (!query.isEmpty() && visible > 0)
            group->setExpanded(true);
    }
}

void MainWindow::openWorldFolder()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("World folder (the one with level.dat) or a server folder"));
    if (!dir.isEmpty() && !addWorldFolder(dir))
        QMessageBox::warning(this, tr("Open world"),
                             tr("There is neither level.dat nor server.properties here: this is not a Minecraft "
                                "world or server."));
}

bool MainWindow::addWorldFolder(const QString& dir)
{
    const auto world = justnbt::probeWorld(dir);
    if (!world)
        return false;

    auto select = [this](const QString& path) {
        for (QTreeWidgetItemIterator it(worldsTree_); *it; ++it) {
            const QVariant v = (*it)->data(0, kWorldIndexRole);
            if (v.isValid() && QDir(worlds_[v.toInt()].path) == QDir(path))
                worldsTree_->setCurrentItem(*it);
        }
    };
    for (const WorldInfo& listed : std::as_const(worlds_)) {
        if (QDir(listed.path) == QDir(world->path)) {
            select(listed.path);
            return true;
        }
    }
    QStringList manual = justnbt::settings().value(kManualWorldsKey).toStringList();
    if (!manual.contains(world->path))
        manual << world->path;
    justnbt::settings().setValue(kManualWorldsKey, manual);
    refreshWorlds();
    select(world->path);
    return true;
}

void MainWindow::openNbtFile()
{
    QStringList patterns;
    for (const QString& ext : justnbt::nbtFileExtensions())
        patterns << QStringLiteral("*.") + ext;
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("Open NBT file"), QString(),
        tr("NBT (%1);;All files (*)").arg(patterns.join(QLatin1Char(' '))));
    openPaths(paths);
}

void MainWindow::openPaths(const QStringList& paths)
{
    if (paths.isEmpty())
        return;
    const bool several = paths.size() > 1;
    QStringList failed;
    for (const QString& path : paths) {
        const QFileInfo info(path);
        if (info.isDir()) {
            if (addWorldFolder(info.absoluteFilePath()))
                continue;
            if (!several) {
                QMessageBox::warning(this, tr("Open world"),
                                     tr("There is neither level.dat nor server.properties in %1: this is not a "
                                        "Minecraft world or server.")
                                         .arg(QDir::toNativeSeparators(info.absoluteFilePath())));
                return;
            }
            failed << info.fileName();
            continue;
        }
        quiet_ = several;
        const bool opened = info.isFile() && openEditor(info.absoluteFilePath(), {}, info.fileName(), nullptr);
        quiet_ = false;
        if (!opened && (several || !info.isFile()))
            failed << (info.fileName().isEmpty() ? path : info.fileName());
    }
    if (!failed.isEmpty())
        QMessageBox::warning(this, tr("Cannot open"),
                             tr("These are not NBT files or worlds, or they are damaged:") + QLatin1Char('\n')
                                 + failed.join(QLatin1Char('\n')));
}

void MainWindow::openPathsLater(const QStringList& paths)
{
    QTimer::singleShot(0, this, [this, paths] { openPaths(paths); });
}

void MainWindow::bringToFront()
{
    setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
    show();
    raise();
    activateWindow();
}

namespace {
QStringList localPaths(const QMimeData* data)
{
    QStringList paths;
    if (!data || !data->hasUrls())
        return paths;
    for (const QUrl& url : data->urls())
        if (url.isLocalFile())
            paths << url.toLocalFile();
    return paths;
}
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (!localPaths(event->mimeData()).isEmpty())
        event->acceptProposedAction();
}

void MainWindow::dragMoveEvent(QDragMoveEvent* event)
{
    if (!localPaths(event->mimeData()).isEmpty())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event)
{
    const QStringList paths = localPaths(event->mimeData());
    if (paths.isEmpty())
        return;
    event->acceptProposedAction();
    openPathsLater(paths);
}

void MainWindow::addWorldsRoot()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("A folder that holds worlds (the saves folder of another launcher, for example)"));
    if (dir.isEmpty())
        return;
    QStringList roots = justnbt::settings().value(kExtraRootsKey).toStringList();
    if (!roots.contains(dir)) {
        roots << dir;
        justnbt::settings().setValue(kExtraRootsKey, roots);
    }
    refreshWorlds();
}

void MainWindow::chooseLanguage(const QString& language)
{
    if (language == justnbt::configuredLanguage())
        return;
    justnbt::setConfiguredLanguage(language);
    const auto answer = QMessageBox::question(
        this, tr("Language"),
        tr("The language changes after a restart.\n\nRestart JustNBT now?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (answer != QMessageBox::Yes)
        return;
    if (close())
        QProcess::startDetached(QApplication::applicationFilePath(), {});
}

void MainWindow::chooseMinecraftDir()
{
    MinecraftSourceDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    justnbt::setMinecraftSource(dialog.chosenDir(), dialog.chosenVersion());
    justnbt::GameAssets& assets = justnbt::GameAssets::instance();
    assets.reload();
    justnbt::clearItemIcons();
    for (int i = 0; i < tabs_->count(); ++i)
        if (auto* editor = editorAt(i))
            editor->refreshView();
    statusBar()->showMessage(assets.describeSource(), 10000);
}

bool MainWindow::openEditor(const QString& path, const QByteArray& dbKey, const QString& title,
                            const WorldInfo* world)
{
    const QString clean = QFileInfo(path).absoluteFilePath();
    const QString identity = dbKey.isEmpty() ? clean : clean + QStringLiteral("#") + justnbt::keyToText(dbKey);
    for (int i = 0; i < tabs_->count(); ++i) {
        if (tabs_->widget(i)->property("identity").toString() == identity) {
            tabs_->setCurrentIndex(i);
            return true;
        }
    }

    std::unique_ptr<justnbt::NbtFile> file;
    try {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        file = dbKey.isEmpty() ? justnbt::NbtFile::load(path) : justnbt::NbtFile::loadDbRecord(path, dbKey);
        QApplication::restoreOverrideCursor();
    } catch (const std::exception& e) {
        QApplication::restoreOverrideCursor();
        if (!quiet_)
            QMessageBox::critical(this, tr("Cannot open"),
                                  QDir::toNativeSeparators(path) + QStringLiteral("\n\n")
                                      + QString::fromUtf8(e.what()));
        return false;
    }

    QString backupSubdir = QStringLiteral("files");
    QString lockFile;
    if (world) {
        QString rel = QDir(world->path).relativeFilePath(QFileInfo(clean).absolutePath());
        if (rel.startsWith(QLatin1String("..")))
            rel = QFileInfo(clean).absoluteDir().dirName();
        backupSubdir = world->folderName;
        if (dbKey.isEmpty() && rel != QLatin1String("."))
            backupSubdir += '/' + rel;
        lockFile = world->lockFilePath();
    }

    addEditorTab(std::move(file), identity, title, backupSubdir, lockFile);
    QWidget* tab = tabs_->currentWidget();
    tagTabForSession(tab, QStringLiteral("file"), world ? world->path : QString());
    tab->setProperty("sessionPath", clean);
    tab->setProperty("sessionKey", QString::fromLatin1(dbKey.toHex()));
    return true;
}

void MainWindow::addEditorTab(std::unique_ptr<justnbt::NbtFile> file, const QString& identity, const QString& title,
                              const QString& backupSubdir, const QString& lockFile)
{
    auto* editor = new NbtEditor(std::move(file), backupSubdir, lockFile);
    editor->setProperty("title", title);
    editor->setProperty("identity", identity);
    undoGroup_->addStack(editor->undoStack());
    connect(editor, &NbtEditor::modifiedChanged, this, [this, editor] { updateTabTitle(editor); });
    connect(editor, &NbtEditor::statusMessage, this, [this](const QString& text) {
        statusBar()->showMessage(text, 8000);
    });
    connect(editor->inventoryPanel(), &InventoryPanel::showOnMap, this,
            [this, editor](const QString& dimension, int x, int z) { showOnMap(editor, dimension, x, z); });

    const int index = tabs_->addTab(editor, title);
    tabs_->setTabToolTip(index, QDir::toNativeSeparators(identity));
    tabs_->setCurrentIndex(index);
    stack_->setCurrentWidget(tabs_);
}

bool MainWindow::activateTab(const QString& identity)
{
    for (int i = 0; i < tabs_->count(); ++i) {
        if (tabs_->widget(i)->property("identity").toString() == identity) {
            tabs_->setCurrentIndex(i);
            return true;
        }
    }
    return false;
}

void MainWindow::openChunk(const WorldInfo& world, const justnbt::Dimension& dimension, int chunkX, int chunkZ,
                           const QString& kind)
{
    const QString where = tr("chunk %1, %2 (%3)").arg(chunkX).arg(chunkZ).arg(dimension.label);
    QString path, identity, title, backupSubdir;
    int dimensionNumber = 0;
    if (world.edition == Edition::Bedrock) {
        path = world.path + QStringLiteral("/db");
        dimensionNumber = dimension.bedrockId;
        title = world.name + QStringLiteral(" — ") + where;
        backupSubdir = world.folderName;
    } else {
        path = dimension.path + QStringLiteral("/%1/r.%2.%3.mca").arg(kind).arg(chunkX >> 5).arg(chunkZ >> 5);
        const QString what = kind == QLatin1String("entities") ? tr("entities, ")
            : kind == QLatin1String("poi")                    ? QStringLiteral("POI, ")
                                                              : QString();
        title = world.name + QStringLiteral(" — ") + what + where;
        QString rel = QDir(world.path).relativeFilePath(QFileInfo(path).absolutePath());
        if (rel.startsWith(QLatin1String("..")))
            rel = QDir(QFileInfo(path).absolutePath() + QStringLiteral("/../..")).dirName() + '/'
                + QDir(QFileInfo(path).absolutePath()).dirName();
        backupSubdir = world.folderName + '/' + rel;
    }
    identity = justnbt::NbtFile::chunkIdentity(path, dimensionNumber, chunkX, chunkZ);
    if (activateTab(identity))
        return;

    std::unique_ptr<justnbt::NbtFile> file;
    try {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        file = world.edition == Edition::Bedrock ? justnbt::NbtFile::loadBedrockChunk(path, dimensionNumber, chunkX, chunkZ)
                                                 : justnbt::NbtFile::loadRegionChunk(path, chunkX, chunkZ);
        QApplication::restoreOverrideCursor();
    } catch (const std::exception& e) {
        QApplication::restoreOverrideCursor();
        if (!quiet_)
            QMessageBox::information(this, tr("Chunk %1, %2").arg(chunkX).arg(chunkZ),
                                     QString::fromUtf8(e.what()));
        return;
    }
    addEditorTab(std::move(file), identity, title, backupSubdir, world.lockFilePath());
    QWidget* tab = tabs_->currentWidget();
    tagTabForSession(tab, QStringLiteral("chunk"), world.path);
    tab->setProperty("sessionDimension", dimension.id);
    tab->setProperty("sessionChunkX", chunkX);
    tab->setProperty("sessionChunkZ", chunkZ);
    tab->setProperty("sessionChunkKind", kind);

    auto* editor = qobject_cast<NbtEditor*>(tabs_->currentWidget());
    const QString mapIdentity = MapTab::identityFor(world);
    connect(editor, &NbtEditor::saved, this, [this, mapIdentity] {
        for (int i = 0; i < tabs_->count(); ++i)
            if (tabs_->widget(i)->property("identity").toString() == mapIdentity)
                if (auto* map = qobject_cast<MapTab*>(tabs_->widget(i)))
                    map->refresh();
    });
}

NbtEditor* MainWindow::editorAt(int index) const
{
    return qobject_cast<NbtEditor*>(tabs_->widget(index));
}

void MainWindow::updateTabTitle(NbtEditor* editor)
{
    const int index = tabs_->indexOf(editor);
    if (index < 0)
        return;
    const QString title = editor->property("title").toString();
    tabs_->setTabText(index, editor->isModified() ? QStringLiteral("● ") + title : title);
}

void MainWindow::showOnMap(QWidget* tab, QString dimensionId, int x, int z)
{
    std::optional<WorldInfo> world;
    const QString worldPath = tab->property("sessionWorld").toString();
    if (!worldPath.isEmpty())
        world = justnbt::probeWorld(worldPath);
    QDir dir = QFileInfo(tab->property("sessionPath").toString()).absoluteDir();
    for (int up = 0; !world && up < 4 && !tab->property("sessionPath").toString().isEmpty(); ++up) {
        world = justnbt::probeWorld(dir.absolutePath());
        if (!dir.cdUp())
            break;
    }
    if (!world || world->dimensions.isEmpty()) {
        statusBar()->showMessage(tr("This file does not lie in a world folder, so there is no map to show"), 8000);
        return;
    }
    if (dimensionId.isEmpty())
        dimensionId = tab->property("sessionDimension").toString();
    bool known = false;
    for (const auto& d : world->dimensions)
        known = known || d.id == dimensionId;
    if (!known)
        dimensionId = world->dimensions.first().id;
    openMap(*world, dimensionId);
    if (auto* map = qobject_cast<MapTab*>(tabs_->currentWidget()))
        map->showPlace(x, z);
}

void MainWindow::openMap(const WorldInfo& world, const QString& dimensionId)
{
    const QString identity = MapTab::identityFor(world);
    for (int i = 0; i < tabs_->count(); ++i) {
        if (tabs_->widget(i)->property("identity").toString() == identity) {
            tabs_->setCurrentIndex(i);
            if (auto* map = qobject_cast<MapTab*>(tabs_->widget(i)))
                map->showDimension(dimensionId);
            return;
        }
    }
    auto* map = new MapTab(world, dimensionId);
    map->setProperty("identity", identity);
    tagTabForSession(map, QStringLiteral("map"), world.path);
    connect(map, &MapTab::openChunk, this, [this, world](const justnbt::Dimension& d, int cx, int cz, const QString& kind) {
        openChunk(world, d, cx, cz, kind);
    });
    const int index = tabs_->addTab(map, world.name + tr(" — map"));
    tabs_->setTabToolTip(index, QDir::toNativeSeparators(world.path));
    tabs_->setCurrentIndex(index);
    stack_->setCurrentWidget(tabs_);
}

bool MainWindow::closeTab(int index)
{
    QWidget* widget = tabs_->widget(index);
    if (!widget)
        return true;
    NbtEditor* editor = editorAt(index);
    if (editor && editor->isModified()) {
        tabs_->setCurrentIndex(index);
        const auto answer = QMessageBox::question(
            this, tr("Unsaved changes"),
            tr("Save the changes in %1?").arg(editor->property("title").toString()),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
        if (answer == QMessageBox::Cancel)
            return false;
        if (answer == QMessageBox::Save && !editor->save())
            return false;
    }
    tabs_->removeTab(index);
    widget->deleteLater();
    if (tabs_->count() == 0)
        stack_->setCurrentIndex(0);
    return true;
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    saveSession();
    for (int i = tabs_->count() - 1; i >= 0; --i) {
        if (!closeTab(i)) {
            event->ignore();
            return;
        }
    }
    justnbt::settings().setValue(QStringLiteral("window/geometry"), saveGeometry());
    justnbt::settings().sync();
    event->accept();
}

void MainWindow::tagTabForSession(QWidget* tab, const QString& kind, const QString& worldPath)
{
    tab->setProperty("sessionKind", kind);
    tab->setProperty("sessionWorld", worldPath);
}

void MainWindow::saveSession()
{
    QSettings& config = justnbt::settings();
    config.remove(kSessionArray);
    if (!config.value(kRestoreTabsKey, true).toBool())
        return;

    config.beginWriteArray(kSessionArray);
    int written = 0;
    for (int i = 0; i < tabs_->count(); ++i) {
        QWidget* tab = tabs_->widget(i);
        const QString kind = tab->property("sessionKind").toString();
        if (kind.isEmpty())
            continue;
        config.setArrayIndex(written++);
        config.setValue(QStringLiteral("kind"), kind);
        config.setValue(QStringLiteral("world"), tab->property("sessionWorld"));
        config.setValue(QStringLiteral("title"), tab->property("title"));
        if (kind == QLatin1String("file")) {
            config.setValue(QStringLiteral("path"), tab->property("sessionPath"));
            config.setValue(QStringLiteral("key"), tab->property("sessionKey"));
        } else if (kind == QLatin1String("chunk")) {
            config.setValue(QStringLiteral("dimension"), tab->property("sessionDimension"));
            config.setValue(QStringLiteral("x"), tab->property("sessionChunkX"));
            config.setValue(QStringLiteral("z"), tab->property("sessionChunkZ"));
            config.setValue(QStringLiteral("chunkKind"), tab->property("sessionChunkKind"));
        } else if (auto* map = qobject_cast<MapTab*>(tab)) {
            config.setValue(QStringLiteral("dimension"), map->currentDimensionId());
        }
        if (tab == tabs_->currentWidget())
            config.setValue(QStringLiteral("active"), true);
    }
    config.endArray();
}

void MainWindow::restoreSession()
{
    QSettings& config = justnbt::settings();
    if (!config.value(kRestoreTabsKey, true).toBool())
        return;

    struct Entry {
        QString kind, world, title, path, dimension, chunkKind;
        QByteArray key;
        int x = 0, z = 0;
        bool active = false;
    };
    std::vector<Entry> entries;
    const int count = config.beginReadArray(kSessionArray);
    for (int i = 0; i < count; ++i) {
        config.setArrayIndex(i);
        Entry e;
        e.kind = config.value(QStringLiteral("kind")).toString();
        e.world = config.value(QStringLiteral("world")).toString();
        e.title = config.value(QStringLiteral("title")).toString();
        e.path = config.value(QStringLiteral("path")).toString();
        e.key = QByteArray::fromHex(config.value(QStringLiteral("key")).toString().toLatin1());
        e.dimension = config.value(QStringLiteral("dimension")).toString();
        e.chunkKind = config.value(QStringLiteral("chunkKind")).toString();
        e.x = config.value(QStringLiteral("x")).toInt();
        e.z = config.value(QStringLiteral("z")).toInt();
        e.active = config.value(QStringLiteral("active")).toBool();
        entries.push_back(e);
    }
    config.endArray();
    if (entries.empty())
        return;

    quiet_ = true;
    QHash<QString, std::optional<WorldInfo>> knownWorlds;
    auto worldOf = [&](const QString& path) -> const WorldInfo* {
        if (path.isEmpty())
            return nullptr;
        auto it = knownWorlds.find(path);
        if (it == knownWorlds.end())
            it = knownWorlds.insert(path, justnbt::probeWorld(path));
        return it->has_value() ? &it->value() : nullptr;
    };

    QWidget* active = nullptr;
    int skipped = 0;
    for (const Entry& e : entries) {
        const int before = tabs_->count();
        const WorldInfo* world = worldOf(e.world);
        if (!e.world.isEmpty() && !world) {
            ++skipped;
            continue;
        }
        if (e.kind == QLatin1String("file")) {
            if (e.path.isEmpty() || !QFileInfo::exists(e.path)) {
                ++skipped;
                continue;
            }
            openEditor(e.path, e.key, e.title, world);
        } else if (e.kind == QLatin1String("map") && world) {
            openMap(*world, e.dimension);
        } else if (e.kind == QLatin1String("chunk") && world) {
            const auto dims = world->dimensions;
            const auto it = std::find_if(dims.begin(), dims.end(),
                                         [&](const justnbt::Dimension& d) { return d.id == e.dimension; });
            if (it == dims.end()) {
                ++skipped;
                continue;
            }
            openChunk(*world, *it, e.x, e.z, e.chunkKind);
        } else {
            ++skipped;
            continue;
        }
        if (tabs_->count() == before)
            ++skipped;
        else if (e.active)
            active = tabs_->currentWidget();
    }
    quiet_ = false;

    if (active)
        tabs_->setCurrentWidget(active);
    const int opened = tabs_->count();
    if (opened > 0)
        statusBar()->showMessage(skipped == 0
                                     ? tr("Tabs restored: %1").arg(opened)
                                     : tr("Tabs restored: %1, skipped: %2 (those files are gone)")
                                           .arg(opened)
                                           .arg(skipped),
                                 8000);
}

void MainWindow::showWorldMenu(const QPoint& pos)
{
    QTreeWidgetItem* item = worldsTree_->itemAt(pos);
    if (!item || !item->data(0, kWorldIndexRole).isValid())
        return;
    const WorldInfo world = worlds_[item->data(0, kWorldIndexRole).toInt()];
    const QString source = item->data(0, kWorldSourceRole).toString();

    QMenu menu(this);
    menu.addAction(tr("Open the world folder"), this,
                   [world] { QDesktopServices::openUrl(QUrl::fromLocalFile(world.path)); });
    if (source == QLatin1String("manual"))
        menu.addAction(tr("Remove this world from the list"), this, [this, world, source] {
            forgetWorld(world, source);
        });
    else if (!source.isEmpty())
        menu.addAction(tr("Remove the folder %1 from the list").arg(QDir(source).dirName()), this,
                       [this, world, source] { forgetWorld(world, source); });
    menu.exec(worldsTree_->viewport()->mapToGlobal(pos));
}

int MainWindow::closeTabsOfWorld(const WorldInfo& world)
{
    const QString path = QDir(world.path).absolutePath();
    int closed = 0;
    for (int i = tabs_->count() - 1; i >= 0; --i) {
        const QString identity = QDir::fromNativeSeparators(tabs_->widget(i)->property("identity").toString());
#ifdef Q_OS_WIN
        const Qt::CaseSensitivity pathCase = Qt::CaseInsensitive;
#else
        const Qt::CaseSensitivity pathCase = Qt::CaseSensitive;
#endif
        if (identity.startsWith(path, pathCase) && closeTab(i))
            ++closed;
    }
    return closed;
}

void MainWindow::forgetWorld(const WorldInfo& world, const QString& source)
{
    const QString question = source == QLatin1String("manual")
        ? tr("Remove the world %1 from the list?").arg(world.name)
        : tr("Remove the folder %1 and all the worlds in it from the list?").arg(QDir(source).dirName());
    if (QMessageBox::question(this, tr("Remove from the list"),
                              question + tr("\n\nThe files of the world are not touched."))
        != QMessageBox::Yes)
        return;

    closeTabsOfWorld(world);
    const QString key = source == QLatin1String("manual") ? kManualWorldsKey : kExtraRootsKey;
    const QString value = source == QLatin1String("manual") ? world.path : source;
    QStringList list = justnbt::settings().value(key).toStringList();
    list.removeAll(value);
    justnbt::settings().setValue(key, list);
    refreshWorlds();
}
