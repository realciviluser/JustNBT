#include "ui/NbtModel.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QTranslator>

#include "core/Config.h"
#include <QHeaderView>
#include <QTreeView>
#include <QUndoStack>

using nbt::Tag;
using nbt::TagType;

static Tag* add(Tag* parent, TagType type, const char* name)
{
    return parent->append(std::make_unique<Tag>(type, name));
}

int main(int argc, char** argv)
{
    QApplication::setOrganizationName(QStringLiteral("JustNBT_Snapshot"));
    QApplication::setApplicationName(QStringLiteral("JustNBT_Snapshot"));
    QApplication app(argc, argv);
    if (argc < 2)
        return 2;
    const QString qmPath = argc >= 3 ? QString::fromLocal8Bit(argv[2]) : QString();
    const bool russian = qmPath.contains(QLatin1String("_ru"));
    justnbt::setConfiguredLanguage(russian ? QStringLiteral("ru") : QStringLiteral("en"));
    QTranslator translator;
    if (!qmPath.isEmpty() && translator.load(qmPath))
        QApplication::installTranslator(&translator);

    auto root = std::make_unique<Tag>(TagType::Compound, "");
    Tag* data = add(root.get(), TagType::Compound, "Data");
    add(data, TagType::Float, "Health")->floating = 20;
    add(data, TagType::String, "LevelName")->string = "My world";
    Tag* inv = add(data, TagType::List, "Inventory");
    inv->listType = TagType::Compound;
    for (const char* item : {"minecraft:stone", "minecraft:torch"}) {
        Tag* e = inv->append(std::make_unique<Tag>(TagType::Compound));
        add(e, TagType::String, "id")->string = item;
        add(e, TagType::Byte, "Count")->integer = 1;
    }
    {
        Tag* e = inv->append(std::make_unique<Tag>(TagType::Compound));
        add(e, TagType::String, "id")->string = "minecraft:diamond_sword";
        add(e, TagType::Byte, "Count")->integer = 1;
        Tag* tag = add(e, TagType::Compound, "tag");
        add(tag, TagType::Int, "Damage")->integer = 41;
        Tag* display = add(tag, TagType::Compound, "display");
        add(display, TagType::String, "Name")->string = "{\"text\":\"Крушитель\"}";
        Tag* ench = add(tag, TagType::List, "Enchantments");
        ench->listType = TagType::Compound;
        for (const auto& [id, level] : {std::pair<const char*, int>{"minecraft:sharpness", 5},
                                        {"minecraft:unbreaking", 3}}) {
            Tag* one = ench->append(std::make_unique<Tag>(TagType::Compound));
            add(one, TagType::String, "id")->string = id;
            add(one, TagType::Short, "lvl")->integer = level;
        }
    }
    Tag* other = add(root.get(), TagType::Compound, "Other");
    add(other, TagType::Int, "Untouched")->integer = 7;

    QUndoStack undo;
    NbtModel model(root.get(), QStringLiteral("level.dat"), &undo);

    model.setData(model.indexFromTag(data->child("Health"), NbtModel::ValueColumn), QStringLiteral("12,5"),
                  Qt::EditRole);
    auto fresh = std::make_unique<Tag>(TagType::String, "NewTag");
    fresh->string = "added";
    model.insertTag(model.indexFromTag(data), int(data->children.size()), std::move(fresh));
    model.removeTag(model.indexFromTag(inv->children[1].get()));

    QTreeView view;
    view.setModel(&model);
    view.setAlternatingRowColors(true);
    view.expandAll();
    view.header()->resizeSection(0, 240);
    view.header()->resizeSection(1, 90);
    view.resize(860, 420);
    const bool saved = view.grab().save(QString::fromLocal8Bit(argv[1]));
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir(appData).removeRecursively();
    QDir().rmdir(QFileInfo(appData).absolutePath());
    return saved ? 0 : 1;
}
