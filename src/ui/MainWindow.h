#pragma once

#include "core/NbtFile.h"
#include "core/Worlds.h"

#include <QMainWindow>

class NbtEditor;
class QLabel;
class QLineEdit;
class QStackedWidget;
class QTabWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QUndoGroup;
class QWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow();

    void openPaths(const QStringList& paths);
    void openPathsLater(const QStringList& paths);
    void bringToFront();

protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void buildMenus();
    void refreshWorlds();
    void addWorldItem(QTreeWidgetItem* group, const justnbt::WorldInfo& world);
    void showWorld(int worldIndex);
    void filterFiles(const QString& text);
    void openWorldFolder();
    bool addWorldFolder(const QString& dir);
    void openNbtFile();
    void addWorldsRoot();
    void chooseMinecraftDir();
    void chooseLanguage(const QString& language);
    void saveSession();
    void restoreSession();
    void showWorldMenu(const QPoint& pos);
    void forgetWorld(const justnbt::WorldInfo& world, const QString& source);
    int closeTabsOfWorld(const justnbt::WorldInfo& world);
    bool openEditor(const QString& path, const QByteArray& dbKey, const QString& title,
                    const justnbt::WorldInfo* world);
    void addEditorTab(std::unique_ptr<justnbt::NbtFile> file, const QString& identity, const QString& title,
                      const QString& backupSubdir, const QString& lockFile);
    bool activateTab(const QString& identity);
    void openMap(const justnbt::WorldInfo& world, const QString& dimensionId);
    void showOnMap(QWidget* tab, QString dimensionId, int x, int z);
    void tagTabForSession(QWidget* tab, const QString& kind, const QString& worldPath);
    void openChunk(const justnbt::WorldInfo& world, const justnbt::Dimension& dimension, int chunkX, int chunkZ,
                   const QString& kind);
    bool closeTab(int index);
    void updateTabTitle(NbtEditor* editor);
    NbtEditor* editorAt(int index) const;

    QList<justnbt::WorldInfo> worlds_;
    QList<justnbt::WorldInfo> manualWorlds_;
    int currentWorld_ = -1;
    bool quiet_ = false;

    QTreeWidget* worldsTree_;
    QLabel* worldTitle_;
    QLabel* lockWarning_;
    QLineEdit* filesFilter_;
    QTreeWidget* filesTree_;
    QStackedWidget* stack_;
    QTabWidget* tabs_;
    QUndoGroup* undoGroup_;
};
