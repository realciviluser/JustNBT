#include "ui/MaintenanceDialog.h"

#include "core/Backup.h"
#include "core/Maintenance.h"
#include "core/MapRender.h"

#include <QApplication>
#include <QCheckBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QUrl>
#include <QVBoxLayout>

namespace {
QString sizeText(qint64 bytes)
{
    return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}

QPushButton* openFolderButton(const QString& dir, QWidget* parent)
{
    auto* b = new QPushButton(MaintenanceDialog::tr("Open the folder"), parent);
    QObject::connect(b, &QPushButton::clicked, parent, [dir] {
        QDir().mkpath(dir);
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });
    return b;
}

template <class F>
auto busy(F&& fn)
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    auto r = fn();
    QApplication::restoreOverrideCursor();
    return r;
}
}

MaintenanceDialog::MaintenanceDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Backups and cache"));
    const justnbt::BackupPolicy policy = justnbt::loadBackupPolicy();

    auto* backups = new QGroupBox(tr("Backups"));
    backupStats_ = new QLabel;
    auto* path = new QLabel(QDir::toNativeSeparators(justnbt::backupRoot()));
    path->setTextInteractionFlags(Qt::TextSelectableByMouse);

    autoCleanup_ = new QCheckBox(tr("Delete old backups automatically (when the program starts)"));
    autoCleanup_->setChecked(policy.autoCleanup);
    keepPerFile_ = new QSpinBox;
    keepPerFile_->setRange(1, 1000);
    keepPerFile_->setValue(policy.keepPerFile);
    keepPerFile_->setSuffix(tr(" backups of every file"));
    keepDays_ = new QSpinBox;
    keepDays_->setRange(0, 3650);
    keepDays_->setValue(policy.keepDays);
    keepDays_->setSuffix(tr(" days"));

    auto* rules = new QFormLayout;
    rules->addRow(tr("Always keep the last:"), keepPerFile_);
    rules->addRow(tr("And every backup from the last:"), keepDays_);
    auto* rulesHint = new QLabel(tr("A backup is deleted only when it is older than that and is not among the last "
                                    "backups of its own file."));
    rulesHint->setWordWrap(true);

    auto* cleanNow = new QPushButton(tr("Clean up by these rules now"));
    auto* removeAll = new QPushButton(tr("Delete all backups…"));
    auto* backupButtons = new QHBoxLayout;
    backupButtons->addWidget(cleanNow);
    backupButtons->addWidget(removeAll);
    backupButtons->addStretch();
    backupButtons->addWidget(openFolderButton(justnbt::backupRoot(), this));

    auto* bl = new QVBoxLayout(backups);
    bl->addWidget(path);
    bl->addWidget(backupStats_);
    bl->addWidget(autoCleanup_);
    bl->addLayout(rules);
    bl->addWidget(rulesHint);
    bl->addLayout(backupButtons);

    auto* cache = new QGroupBox(tr("Map cache"));
    cacheStats_ = new QLabel;
    auto* cachePath = new QLabel(QDir::toNativeSeparators(justnbt::tileCacheRoot()));
    cachePath->setTextInteractionFlags(Qt::TextSelectableByMouse);
    cacheLimitMb_ = new QSpinBox;
    cacheLimitMb_->setRange(16, 1000000);
    cacheLimitMb_->setSingleStep(256);
    cacheLimitMb_->setValue(int(justnbt::loadCacheLimitBytes() >> 20));
    cacheLimitMb_->setSuffix(tr(" MB"));
    auto* cacheForm = new QFormLayout;
    cacheForm->addRow(tr("At most:"), cacheLimitMb_);
    auto* cacheHint = new QLabel(tr("The cache can be deleted at any time: the map is simply drawn again."));
    cacheHint->setWordWrap(true);
    auto* clearCache = new QPushButton(tr("Clear the cache"));
    auto* cacheButtons = new QHBoxLayout;
    cacheButtons->addWidget(clearCache);
    cacheButtons->addStretch();
    cacheButtons->addWidget(openFolderButton(justnbt::tileCacheRoot(), this));

    auto* cl = new QVBoxLayout(cache);
    cl->addWidget(cachePath);
    cl->addWidget(cacheStats_);
    cl->addLayout(cacheForm);
    cl->addWidget(cacheHint);
    cl->addLayout(cacheButtons);

    result_ = new QLabel;
    result_->setWordWrap(true);
    auto* close = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(close, &QDialogButtonBox::rejected, this, &QDialog::accept);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(backups);
    layout->addWidget(cache);
    layout->addWidget(result_);
    layout->addWidget(close);
    resize(560, sizeHint().height());

    connect(autoCleanup_, &QCheckBox::toggled, this, &MaintenanceDialog::savePolicy);
    connect(keepPerFile_, &QSpinBox::valueChanged, this, &MaintenanceDialog::savePolicy);
    connect(keepDays_, &QSpinBox::valueChanged, this, &MaintenanceDialog::savePolicy);
    connect(cacheLimitMb_, &QSpinBox::valueChanged, this, [](int mb) { justnbt::saveCacheLimitBytes(qint64(mb) << 20); });

    connect(cleanNow, &QPushButton::clicked, this, [this] {
        savePolicy();
        const auto r = busy([] { return justnbt::cleanupBackups(justnbt::backupRoot(), justnbt::loadBackupPolicy()); });
        report(tr("Old backups deleted"), r.removed, r.freedBytes);
    });
    connect(removeAll, &QPushButton::clicked, this, [this] {
        const auto answer = QMessageBox::warning(
            this, tr("Delete all backups"),
            tr("Delete every backup of every world? This cannot be undone.\n\nThe worlds themselves are not touched."),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (answer != QMessageBox::Yes)
            return;
        const auto r = busy([] { return justnbt::removeAllBackups(justnbt::backupRoot()); });
        report(tr("Backup files deleted"), r.removed, r.freedBytes);
    });
    connect(clearCache, &QPushButton::clicked, this, [this] {
        const auto r = busy([] { return justnbt::clearTileCache(justnbt::tileCacheRoot()); });
        report(tr("Cache tiles deleted"), r.removed, r.freedBytes);
    });

    refreshStats();
}

void MaintenanceDialog::savePolicy()
{
    justnbt::saveBackupPolicy({autoCleanup_->isChecked(), keepPerFile_->value(), keepDays_->value()});
}

void MaintenanceDialog::refreshStats()
{
    const auto b = busy([] { return justnbt::folderStats(justnbt::backupRoot()); });
    const auto c = busy([] { return justnbt::folderStats(justnbt::tileCacheRoot()); });
    backupStats_->setText(tr("Taking up now: %1 (files: %2)").arg(sizeText(b.bytes)).arg(b.files));
    cacheStats_->setText(tr("Taking up now: %1 (tiles: %2)").arg(sizeText(c.bytes)).arg(c.files));
}

void MaintenanceDialog::report(const QString& what, int removed, qint64 bytes)
{
    result_->setText(tr("%1: %2, %3 freed.").arg(what).arg(removed).arg(sizeText(bytes)));
    refreshStats();
}
