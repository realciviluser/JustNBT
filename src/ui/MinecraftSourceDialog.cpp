#include "ui/MinecraftSourceDialog.h"

#include "core/Config.h"
#include "core/GameAssets.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

MinecraftSourceDialog::MinecraftSourceDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Minecraft for the item names and pictures"));
    setMinimumWidth(620);

    auto* intro = new QLabel(tr("JustNBT takes the names of items, blocks and mobs and their pictures from a "
                                "Minecraft Java installed on this computer. Choose its folder: .minecraft of the "
                                "official launcher, or the folder of Prism Launcher or MultiMC (an instance "
                                "folder works too)."));
    intro->setWordWrap(true);

    folder_ = new QLineEdit;
    folder_->setReadOnly(true);
    auto* browse = new QPushButton(tr("Choose a folder…"));
    connect(browse, &QPushButton::clicked, this, &MinecraftSourceDialog::chooseFolder);
    auto* reset = new QPushButton(tr("Default"));
    reset->setToolTip(tr("Back to %1").arg(QDir::toNativeSeparators(justnbt::defaultMinecraftDir())));
    connect(reset, &QPushButton::clicked, this, [this] {
        dir_.clear();
        folder_->setText(QDir::toNativeSeparators(justnbt::defaultMinecraftDir()));
        fillVersions({});
    });
    auto* folderRow = new QHBoxLayout;
    folderRow->addWidget(folder_, 1);
    folderRow->addWidget(browse);
    folderRow->addWidget(reset);

    version_ = new QComboBox;
    version_->setSizeAdjustPolicy(QComboBox::AdjustToContents);

    status_ = new QLabel;
    status_->setWordWrap(true);
    status_->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto* form = new QFormLayout;
    form->addRow(tr("Folder:"), folderRow);
    form->addRow(tr("Version:"), version_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(intro);
    layout->addLayout(form);
    layout->addWidget(status_);
    layout->addStretch(1);
    layout->addWidget(buttons);

    dir_ = justnbt::settings().value(QStringLiteral("assets/minecraftDir")).toString();
    folder_->setText(QDir::toNativeSeparators(justnbt::minecraftDir()));
    fillVersions(justnbt::configuredMinecraftVersion());

    const justnbt::GameAssets& assets = justnbt::GameAssets::instance();
    QString now = tr("Now: %1").arg(assets.describeSource());
    if (assets.available())
        now += QLatin1Char('\n') + QDir::toNativeSeparators(assets.jarPath());
    status_->setText(now);
    resize(660, std::max(layout->totalHeightForWidth(660), sizeHint().height()));
}

void MinecraftSourceDialog::fillVersions(const QString& selectVersion)
{
    const QString dir = justnbt::resolveMinecraftDir(dir_.isEmpty() ? justnbt::defaultMinecraftDir() : dir_);
    version_->clear();
    version_->addItem(tr("The newest"), QString());
    for (const auto& v : justnbt::versionsIn(dir))
        version_->addItem(v.release ? v.id : tr("%1 (snapshot)").arg(v.id), v.id);
    const int at = version_->findData(selectVersion);
    version_->setCurrentIndex(at >= 0 ? at : 0);
    version_->setEnabled(version_->count() > 1);
    if (version_->count() == 1)
        version_->setItemText(0, tr("No versions in this folder"));
}

bool MinecraftSourceDialog::setFolder(const QString& dir)
{
    if (justnbt::versionsIn(justnbt::resolveMinecraftDir(dir)).isEmpty())
        return false;
    dir_ = QDir(dir).absolutePath();
    folder_->setText(QDir::toNativeSeparators(dir_));
    fillVersions({});
    return true;
}

void MinecraftSourceDialog::chooseFolder()
{
    const QString dir =
        QFileDialog::getExistingDirectory(this, tr("Minecraft or launcher folder"), justnbt::minecraftDir());
    if (dir.isEmpty())
        return;
    if (!setFolder(dir))
        QMessageBox::warning(this, tr("No Minecraft here"),
                             tr("No Minecraft Java versions were found in\n%1\n\nChoose the folder of a launcher: "
                                "the one that holds versions/ (the official launcher, .minecraft) or libraries/ "
                                "(Prism Launcher, MultiMC).")
                                 .arg(QDir::toNativeSeparators(dir)));
}

QString MinecraftSourceDialog::chosenVersion() const
{
    return version_->currentData().toString();
}
