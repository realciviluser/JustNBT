#include "core/BedrockDb.h"
#include "core/NbtFile.h"
#include "ui/Inventory.h"
#include "ui/ItemEditorDialog.h"
#include "ui/MinecraftSourceDialog.h"
#include "ui/InventoryPanel.h"
#include "ui/NbtEditor.h"
#include "ui/NbtModel.h"

#include <QApplication>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QTabWidget>
#include <QTreeView>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTranslator>
#include <QUndoStack>

#include <cstdio>

using nbt::Tag;
using nbt::TagType;
using namespace justnbt;

namespace {
int failures = 0;

void check(bool ok, const QString& what)
{
    std::printf("%-66s %s\n", qPrintable(what), ok ? "ok" : "FAILED");
    if (!ok)
        ++failures;
}

Tag* add(Tag* parent, TagType type, const char* name = "")
{
    auto t = std::make_unique<Tag>(type, name);
    if (type == TagType::List)
        t->listType = TagType::Compound;
    return parent->append(std::move(t));
}

void addInt(Tag* parent, TagType type, const char* name, int64_t value)
{
    add(parent, type, name)->integer = value;
}

void addFloat(Tag* parent, const char* name, double value)
{
    add(parent, TagType::Float, name)->floating = value;
}

void addString(Tag* parent, const char* name, const char* value)
{
    add(parent, TagType::String, name)->string = value;
}

Tag* javaItem(Tag* list, int slot, const char* id, int count = 1, int damage = 0)
{
    Tag* item = add(list, TagType::Compound);
    if (slot != -1000)
        addInt(item, TagType::Byte, "Slot", slot);
    addString(item, "id", id);
    addInt(item, TagType::Int, "count", count);
    if (damage) {
        Tag* components = add(item, TagType::Compound, "components");
        addInt(components, TagType::Int, "minecraft:damage", damage);
    }
    return item;
}

Tag* bedrockItem(Tag* list, int slot, const char* name, int count)
{
    Tag* item = add(list, TagType::Compound);
    addInt(item, TagType::Byte, "Count", count);
    addInt(item, TagType::Short, "Damage", 0);
    addString(item, "Name", name);
    addInt(item, TagType::Byte, "WasPickedUp", 0);
    if (slot >= 0)
        addInt(item, TagType::Byte, "Slot", slot);
    return item;
}

void javaPlayerStats(Tag* p)
{
    addFloat(p, "Health", 17);
    addInt(p, TagType::Int, "foodLevel", 20);
    addFloat(p, "foodSaturationLevel", 5);
    addInt(p, TagType::Int, "XpLevel", 30);
    addFloat(p, "XpP", 0.25);
    addInt(p, TagType::Int, "SelectedItemSlot", 0);
    addInt(p, TagType::Int, "playerGameType", 0);
    add(p, TagType::Compound, "abilities");
}

std::unique_ptr<Tag> oldJavaPlayer()
{
    auto p = std::make_unique<Tag>(TagType::Compound);
    addInt(p.get(), TagType::Int, "DataVersion", 3955);
    javaPlayerStats(p.get());
    Tag* inv = add(p.get(), TagType::List, "Inventory");
    javaItem(inv, 0, "minecraft:diamond_sword", 1, 300);
    javaItem(inv, 1, "minecraft:apple", 12);
    javaItem(inv, 20, "minecraft:cobblestone", 64);
    javaItem(inv, 103, "minecraft:iron_helmet");
    javaItem(inv, -106, "minecraft:shield");
    Tag* ender = add(p.get(), TagType::List, "EnderItems");
    javaItem(ender, 4, "minecraft:elytra");
    return p;
}

std::unique_ptr<Tag> newJavaPlayer()
{
    auto p = std::make_unique<Tag>(TagType::Compound);
    addInt(p.get(), TagType::Int, "DataVersion", 4440);
    javaPlayerStats(p.get());
    Tag* inv = add(p.get(), TagType::List, "Inventory");
    javaItem(inv, 0, "minecraft:diamond_sword");
    Tag* equipment = add(p.get(), TagType::Compound, "equipment");
    Tag* head = javaItem(equipment, -1000, "minecraft:turtle_helmet");
    head->name = "head";
    add(p.get(), TagType::List, "EnderItems");
    return p;
}

std::unique_ptr<Tag> bedrockPlayer()
{
    auto p = std::make_unique<Tag>(TagType::Compound);
    Tag* inv = add(p.get(), TagType::List, "Inventory");
    for (int i = 0; i < 36; ++i)
        bedrockItem(inv, i, i == 0 ? "minecraft:iron_sword" : "", i == 0 ? 1 : 0);
    Tag* armor = add(p.get(), TagType::List, "Armor");
    for (int i = 0; i < 4; ++i)
        bedrockItem(armor, -1, "", 0);
    Tag* off = add(p.get(), TagType::List, "Offhand");
    bedrockItem(off, -1, "", 0);
    Tag* ender = add(p.get(), TagType::List, "EnderChestInventory");
    for (int i = 0; i < 27; ++i)
        bedrockItem(ender, i, "", 0);
    Tag* attributes = add(p.get(), TagType::List, "Attributes");
    auto attribute = [&](const char* name, double current, double max) {
        Tag* a = add(attributes, TagType::Compound);
        addString(a, "Name", name);
        addFloat(a, "Current", current);
        addFloat(a, "Max", max);
    };
    attribute("minecraft:health", 20, 20);
    attribute("minecraft:player.hunger", 20, 20);
    attribute("minecraft:player.saturation", 5, 20);
    attribute("minecraft:player.level", 3, 24791);
    attribute("minecraft:player.experience", 0.5, 1);
    addInt(p.get(), TagType::Int, "SelectedInventorySlot", 0);
    addInt(p.get(), TagType::Int, "PlayerGameMode", 0);
    add(p.get(), TagType::Compound, "abilities");
    return p;
}

int count(const Tag* item)
{
    const Tag* c = item->child("count");
    if (!c)
        c = item->child("Count");
    return c ? int(c->integer) : 1;
}

QString idOf(const Tag* item)
{
    if (!item)
        return QStringLiteral("(empty)");
    const Tag* id = item->child("id");
    if (!id)
        id = item->child("Name");
    return QString::fromStdString(id->string);
}

QByteArray bytesOf(const Tag* t)
{
    auto copy = t->clone();
    copy->name.clear();
    return NbtModel::serializeTags({copy.get()});
}

template <class W>
W* field(QWidget* dialog, const char* name)
{
    return dialog->findChild<W*>(QLatin1String(name));
}

struct Doc {
    std::unique_ptr<Tag> root;
    QUndoStack undo;
    std::unique_ptr<NbtModel> model;
    explicit Doc(std::unique_ptr<Tag> r) : root(std::move(r)), model(std::make_unique<NbtModel>(root.get(), "t", &undo))
    {
    }
};
}

int main(int argc, char** argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    QApplication::setOrganizationName(QStringLiteral("JustNBT_InventoryTest"));
    QApplication::setApplicationName(QStringLiteral("JustNBT_InventoryTest"));
    QApplication app(argc, argv);
    QTranslator translator;
    const bool show = argc >= 4 && QByteArray(argv[1]) == "--show";
    if (argc >= (show ? 5 : 3) && translator.load(QString::fromLocal8Bit(argv[show ? 4 : 2])))
        QApplication::installTranslator(&translator);

    if (show) {
        const QString source = QString::fromLocal8Bit(argv[2]);
        std::unique_ptr<NbtFile> file;
        if (source.endsWith(QLatin1String("|player"))) {
            const QString db = source.chopped(7);
            QByteArray key;
            BedrockDb(db).forEachWithPrefix("player_server_", [&](std::string_view k, std::string_view) {
                key = QByteArray(k.data(), qsizetype(k.size()));
                return false;
            });
            file = NbtFile::loadDbRecord(db, key);
        } else if (const QStringList parts = source.split(QLatin1Char('|')); parts.size() == 3) {
            file = NbtFile::loadRegionChunk(parts[0], parts[1].toInt(), parts[2].toInt());
        } else if (parts.size() == 4 && parts[1] == QLatin1String("chunk")) {
            file = NbtFile::loadBedrockChunk(parts[0], 0, parts[2].toInt(), parts[3].toInt());
        } else {
            file = NbtFile::load(source);
        }
        NbtEditor editor(std::move(file), QString(), QString());
        editor.resize(1300, 820);
        editor.setAttribute(Qt::WA_DontShowOnScreen, true);
        editor.show();
        for (int i = 0; i < 3; ++i)
            QApplication::processEvents();
        if (auto* tree = editor.findChild<QTreeView*>(); tree && source.contains(QLatin1Char('|'))) {
            tree->expandToDepth(1);
            tree->setColumnWidth(0, 300);
            QApplication::processEvents();
        }
        InventoryPanel* panel = editor.inventoryPanel();
        int filled = 0;
        for (auto* cell : panel->slotWidgets())
            filled += !cell->isEmpty();
        std::printf("holder %s, shown %d, bedrock %d, %zu slots, %d with items\n",
                    panel->holder() ? "found" : "NOT found", int(panel->isVisible()), int(panel->layout().bedrock),
                    panel->slotWidgets().size(), filled);
        const bool saved = editor.grab().save(QString::fromLocal8Bit(argv[3]));
        const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        QDir(appData).removeRecursively();
        QDir().rmdir(QFileInfo(appData).absolutePath());
        return saved && panel->holder() ? 0 : 1;
    }

    {
        Doc doc(oldJavaPlayer());
        Tag* player = doc.root.get();
        InventoryLayout l = inventoryLayout(player);
        check(l.kind == InventoryLayout::Kind::Player && !l.bedrock, QStringLiteral("old Java player: a Java player"));
        check(l.armor.size() == 4 && l.hands.size() == 1 && l.storage.size() == 27 && l.hotbar.size() == 9
                  && l.ender.size() == 27,
              QStringLiteral("  4 armour, second hand, 27 + 9 slots, ender chest"));
        check(idOf(itemAt(player, l.armor[0].ref)) == "minecraft:iron_helmet"
                  && idOf(itemAt(player, l.hands[0].ref)) == "minecraft:shield",
              QStringLiteral("  the helmet is slot 103, the shield -106"));
        check(idOf(itemAt(player, l.ender[4].ref)) == "minecraft:elytra", QStringLiteral("  elytra in the ender chest"));
        Tag* nested = itemAt(player, l.hotbar[0].ref)->child("components")->child("minecraft:damage");
        check(findInventoryHolder(nested) == player, QStringLiteral("  a tag deep inside an item leads to the player"));

        const auto inventorySize = [&] { return player->child("Inventory")->children.size(); };
        InventoryEditor edit(doc.model.get(), l);
        check(edit.move(l.hotbar[0].ref, l.storage[0].ref), QStringLiteral("sword: hotbar 0 -> slot 9"));
        check(!itemAt(player, l.hotbar[0].ref) && idOf(itemAt(player, l.storage[0].ref)) == "minecraft:diamond_sword"
                  && inventorySize() == 5,
              QStringLiteral("  only its Slot changed"));
        doc.undo.undo();
        check(idOf(itemAt(player, l.hotbar[0].ref)) == "minecraft:diamond_sword" && !itemAt(player, l.storage[0].ref),
              QStringLiteral("  Ctrl+Z puts it back"));

        check(edit.move(l.hotbar[0].ref, l.hotbar[1].ref), QStringLiteral("sword onto the apples"));
        check(idOf(itemAt(player, l.hotbar[1].ref)) == "minecraft:diamond_sword"
                  && idOf(itemAt(player, l.hotbar[0].ref)) == "minecraft:apple",
              QStringLiteral("  they swap places"));
        check(doc.undo.count() == 1, QStringLiteral("  one step in the history for the swap"));
        doc.undo.undo();

        check(edit.move(l.armor[0].ref, l.ender[0].ref), QStringLiteral("helmet into the ender chest"));
        check(!itemAt(player, l.armor[0].ref) && idOf(itemAt(player, l.ender[0].ref)) == "minecraft:iron_helmet"
                  && inventorySize() == 4 && player->child("EnderItems")->children.size() == 2,
              QStringLiteral("  it left Inventory and joined EnderItems"));
        doc.undo.undo();
        check(inventorySize() == 5 && idOf(itemAt(player, l.armor[0].ref)) == "minecraft:iron_helmet",
              QStringLiteral("  and comes back with Ctrl+Z"));

        check(edit.setCount(l.hotbar[1].ref, 40) && count(itemAt(player, l.hotbar[1].ref)) == 40,
              QStringLiteral("count of the apples: 40"));
        check(edit.clear(l.storage[11].ref) && !itemAt(player, l.storage[11].ref) && inventorySize() == 4,
              QStringLiteral("remove the cobblestone"));

        const PlayerStats s = playerStats(player, false);
        check(s.health && s.food && s.xpLevel && s.xpProgress && s.saturation && s.selectedSlot && s.gameMode
                  && s.maxHealth == 20,
              QStringLiteral("stats: health, food, level, saturation, hand, mode"));
        check(edit.setNumber(s.health, 13) && s.health->type == TagType::Float && s.health->floating == 13,
              QStringLiteral("  health set to 13, still a float"));
        check(edit.setNumber(s.food, 6.4) && s.food->type == TagType::Int && s.food->integer == 6,
              QStringLiteral("  food set to 6, still an int"));
    }

    {
        Doc doc(newJavaPlayer());
        Tag* player = doc.root.get();
        InventoryLayout l = inventoryLayout(player);
        check(l.armor[0].ref.place == SlotRef::Place::Named && l.armor[0].ref.name == "head",
              QStringLiteral("1.21.5 player: armour in equipment"));
        check(idOf(itemAt(player, l.armor[0].ref)) == "minecraft:turtle_helmet", QStringLiteral("  the turtle helmet"));
        InventoryEditor edit(doc.model.get(), l);
        check(edit.move(l.hotbar[0].ref, l.hands[0].ref), QStringLiteral("sword into the second hand"));
        const Tag* off = player->child("equipment")->child("offhand");
        check(off && !off->child("Slot") && player->child("Inventory")->children.empty(),
              QStringLiteral("  equipment.offhand, without a Slot; Inventory empty"));
        check(edit.move(l.armor[0].ref, l.hotbar[5].ref), QStringLiteral("helmet into hotbar 5"));
        const Tag* moved = itemAt(player, l.hotbar[5].ref);
        check(moved && moved->child("Slot") && moved->child("Slot")->integer == 5 && moved->name.empty()
                  && !player->child("equipment")->child("head"),
              QStringLiteral("  it got Slot 5 and left equipment"));
        doc.undo.undo();
        doc.undo.undo();
        check(idOf(itemAt(player, l.hotbar[0].ref)) == "minecraft:diamond_sword"
                  && idOf(itemAt(player, l.armor[0].ref)) == "minecraft:turtle_helmet"
                  && !player->child("equipment")->child("offhand"),
              QStringLiteral("  two Ctrl+Z: as it was"));
    }

    {
        Doc doc(bedrockPlayer());
        Tag* player = doc.root.get();
        InventoryLayout l = inventoryLayout(player);
        check(l.kind == InventoryLayout::Kind::Player && l.bedrock, QStringLiteral("Bedrock player"));
        check(!itemAt(player, l.hotbar[1].ref) && idOf(itemAt(player, l.hotbar[0].ref)) == "minecraft:iron_sword",
              QStringLiteral("  empty placeholders count as empty"));
        InventoryEditor edit(doc.model.get(), l);
        Tag* inv = player->child("Inventory");
        check(edit.move(l.hotbar[0].ref, l.hotbar[3].ref), QStringLiteral("sword: hotbar 0 -> 3"));
        check(inv->children.size() == 36 && idOf(inv->children[3].get()) == "minecraft:iron_sword"
                  && inv->children[3]->child("Slot")->integer == 3 && idOf(inv->children[0].get()).isEmpty()
                  && inv->children[0]->child("Slot")->integer == 0,
              QStringLiteral("  36 entries stay, in slot order"));
        check(edit.move(l.hotbar[3].ref, l.armor[1].ref), QStringLiteral("sword into the Armor list"));
        Tag* armor = player->child("Armor");
        check(armor->children.size() == 4 && idOf(armor->children[1].get()) == "minecraft:iron_sword"
                  && !armor->children[1]->child("Slot") && inv->children.size() == 36,
              QStringLiteral("  Armor[1], without a Slot; 4 and 36 entries"));
        const PlayerStats s = playerStats(player, true);
        check(s.health && s.food && s.xpLevel && s.xpProgress && s.saturation && s.gameMode,
              QStringLiteral("  stats from Attributes"));
        check(edit.setNumber(s.health, 7) && s.health->floating == 7, QStringLiteral("  health set to 7"));
    }

    {
        auto chunk = std::make_unique<Tag>(TagType::Compound);
        addInt(chunk.get(), TagType::Int, "DataVersion", 3955);
        Tag* entities = add(chunk.get(), TagType::List, "block_entities");
        Tag* chest = add(entities, TagType::Compound);
        addString(chest, "id", "minecraft:chest");
        addInt(chest, TagType::Int, "x", 10);
        addInt(chest, TagType::Int, "y", 64);
        addInt(chest, TagType::Int, "z", -3);
        javaItem(add(chest, TagType::List, "Items"), 13, "minecraft:diamond", 8);
        Tag* hopper = add(entities, TagType::Compound);
        addString(hopper, "id", "minecraft:hopper");
        Tag* bedrockChest = add(entities, TagType::Compound);
        addString(bedrockChest, "id", "Dispenser");
        Tag* mobs = add(chunk.get(), TagType::List, "Entities");
        Tag* zombie = add(mobs, TagType::Compound);
        addString(zombie, "id", "minecraft:zombie");
        addFloat(zombie, "Health", 20);
        Tag* hands = add(zombie, TagType::List, "HandItems");
        javaItem(hands, -1000, "minecraft:iron_sword");
        add(hands, TagType::Compound);
        Tag* armorItems = add(zombie, TagType::List, "ArmorItems");
        for (int i = 0; i < 4; ++i)
            add(armorItems, TagType::Compound);

        Doc doc(std::move(chunk));
        InventoryLayout c = inventoryLayout(chest);
        check(c.kind == InventoryLayout::Kind::Container && c.container.size() == 27 && c.columns == 9,
              QStringLiteral("chest: 27 slots in rows of 9"));
        check(idOf(itemAt(chest, c.container[13].ref)) == "minecraft:diamond", QStringLiteral("  diamonds in slot 13"));
        check(findInventoryHolder(itemAt(chest, c.container[13].ref)) == chest,
              QStringLiteral("  an item leads to its chest"));
        const InventoryLayout h = inventoryLayout(hopper);
        check(findInventoryHolder(hopper) == hopper && h.container.size() == 5 && h.columns == 5,
              QStringLiteral("hopper without Items: still 5 slots"));
        const InventoryLayout d = inventoryLayout(bedrockChest);
        check(d.bedrock && d.container.size() == 9 && d.columns == 3, QStringLiteral("Bedrock dispenser: 3 x 3"));

        check(!InventoryEditor(doc.model.get(), h).move(c.container[13].ref, c.container[0].ref),
              QStringLiteral("  the hopper has nothing to move"));
        InventoryEditor(doc.model.get(), c).move(c.container[13].ref, c.container[0].ref);
        check(idOf(itemAt(chest, c.container[0].ref)) == "minecraft:diamond", QStringLiteral("chest: diamonds to slot 0"));
        doc.undo.undo();

        const InventoryLayout z = inventoryLayout(zombie);
        check(z.kind == InventoryLayout::Kind::Mob && z.hands.size() == 2 && z.armor.size() == 4,
              QStringLiteral("zombie: two hands and armour"));
        InventoryEditor ze(doc.model.get(), z);
        check(ze.move(z.hands[0].ref, z.hands[1].ref), QStringLiteral("  sword to the second hand"));
        check(hands->children.size() == 2 && hands->children[0]->children.empty()
                  && idOf(hands->children[1].get()) == "minecraft:iron_sword",
              QStringLiteral("  HandItems: [{}, sword]"));
        check(ze.move(z.hands[1].ref, z.armor[0].ref) && idOf(armorItems->children[3].get()) == "minecraft:iron_sword"
                  && armorItems->children.size() == 4,
              QStringLiteral("  a sword on the head: ArmorItems[3]"));
    }

    {
        Doc doc(newJavaPlayer());
        Tag* player = doc.root.get();
        InventoryLayout l = inventoryLayout(player);
        InventoryEditor edit(doc.model.get(), l);
        Effect speed;
        speed.id = QStringLiteral("minecraft:speed");
        speed.amplifier = 1;
        speed.duration = 600;
        check(edit.setEffect(-1, speed), QStringLiteral("effects: speed II added to a 1.21.5 player"));
        const Tag* list = player->child("active_effects");
        check(list && list->children.size() == 1 && list->children[0]->child("id")->string == "minecraft:speed"
                  && list->children[0]->child("amplifier")->integer == 1
                  && list->children[0]->child("duration")->integer == 600,
              QStringLiteral("  active_effects [{id: speed, amplifier: 1, duration: 600}]"));
        speed.duration = -1;
        check(edit.setEffect(0, speed) && activeEffects(player, false)[0].duration == -1,
              QStringLiteral("  made infinite"));
        check(edit.removeEffect(0) && activeEffects(player, false).empty(), QStringLiteral("  and removed"));
        doc.undo.undo();
        check(activeEffects(player, false).size() == 1, QStringLiteral("  Ctrl+Z brings it back"));

        auto old = oldJavaPlayer();
        old->child("DataVersion")->integer = 3465;
        Doc legacy(std::move(old));
        Effect night;
        night.id = QStringLiteral("minecraft:night_vision");
        night.duration = 200;
        check(InventoryEditor(legacy.model.get(), inventoryLayout(legacy.root.get())).setEffect(-1, night)
                  && legacy.root->child("ActiveEffects")
                  && legacy.root->child("ActiveEffects")->children[0]->child("Id")->integer == 16,
              QStringLiteral("  1.20.1: ActiveEffects with Id 16 (night vision)"));
        Effect trial;
        trial.id = QStringLiteral("minecraft:trial_omen");
        check(!InventoryEditor(legacy.model.get(), inventoryLayout(legacy.root.get())).setEffect(-1, trial),
              QStringLiteral("  an effect 1.20.1 cannot store is refused"));

        Doc bedrock(bedrockPlayer());
        Effect absorb;
        absorb.id = QStringLiteral("minecraft:absorption");
        absorb.duration = 100;
        check(InventoryEditor(bedrock.model.get(), inventoryLayout(bedrock.root.get())).setEffect(-1, absorb),
              QStringLiteral("  Bedrock: absorption added"));
        const Tag* be = bedrock.root->child("ActiveEffects")->children[0].get();
        check(be->child("Id")->integer == 22 && be->child("DurationEasy")->integer == 100,
              QStringLiteral("  Id 22 and the durations of every difficulty"));
    }

    {
        Doc doc(newJavaPlayer());
        Tag* player = doc.root.get();
        Tag* box = javaItem(player->child("Inventory"), 5, "minecraft:red_shulker_box");
        Tag* components = add(box, TagType::Compound, "components");
        Tag* contents = add(components, TagType::List, "minecraft:container");
        Tag* entry = add(contents, TagType::Compound);
        Tag* diamond = javaItem(entry, -1000, "minecraft:diamond", 5);
        diamond->name = "item";
        addInt(entry, TagType::Int, "slot", 0);
        doc.model = std::make_unique<NbtModel>(doc.root.get(), "t", &doc.undo);

        check(contentsHolder(box) == components && itemOfHolder(components) == box,
              QStringLiteral("shulker box: its components hold the contents"));
        check(findInventoryHolder(diamond) == components, QStringLiteral("  a diamond inside leads to the box"));
        InventoryLayout l = inventoryLayout(components);
        check(l.kind == InventoryLayout::Kind::Container && l.container.size() == 27
                  && idOf(itemAt(components, l.container[0].ref)) == "minecraft:diamond",
              QStringLiteral("  27 slots, the diamonds in the first"));
        InventoryEditor edit(doc.model.get(), l);
        check(edit.move(l.container[0].ref, l.container[8].ref), QStringLiteral("  diamonds moved to slot 8"));
        check(contents->children.size() == 1 && contents->children[0]->child("slot")->integer == 8
                  && contents->children[0]->child("item")
                  && idOf(contents->children[0]->child("item")) == "minecraft:diamond",
              QStringLiteral("  { item: {...}, slot: 8 }"));
        check(edit.put(l.container[2].ref, edit.newItem(QStringLiteral("minecraft:apple")), QStringLiteral("put")),
              QStringLiteral("  an apple put into slot 2"));
        check(contents->children.size() == 2 && contents->children[0]->child("slot")->integer == 2,
              QStringLiteral("  kept in slot order"));

        auto oldBox = std::make_unique<Tag>(TagType::Compound);
        addString(oldBox.get(), "id", "minecraft:shulker_box");
        addInt(oldBox.get(), TagType::Byte, "Count", 1);
        Tag* blockEntity = add(add(oldBox.get(), TagType::Compound, "tag"), TagType::Compound, "BlockEntityTag");
        Tag* items = add(blockEntity, TagType::List, "Items");
        Tag* oldDiamond = add(items, TagType::Compound);
        addInt(oldDiamond, TagType::Byte, "Slot", 3);
        addString(oldDiamond, "id", "minecraft:diamond");
        addInt(oldDiamond, TagType::Byte, "Count", 2);
        check(contentsHolder(oldBox.get()) == blockEntity && itemOfHolder(blockEntity) == oldBox.get()
                  && inventoryLayout(blockEntity).container.size() == 27,
              QStringLiteral("  the old format: tag.BlockEntityTag, 27 slots"));
    }

    {
        auto chunk = std::make_unique<Tag>(TagType::Compound);
        addInt(chunk.get(), TagType::Int, "DataVersion", 3955);
        Tag* mobs = add(chunk.get(), TagType::List, "Entities");
        Tag* horse = add(mobs, TagType::Compound);
        addString(horse, "id", "minecraft:horse");
        addFloat(horse, "Health", 22);
        add(horse, TagType::List, "ArmorItems");
        Tag* saddle = javaItem(horse, -1000, "minecraft:saddle");
        saddle->name = "SaddleItem";
        Tag* donkey = add(mobs, TagType::Compound);
        addString(donkey, "id", "minecraft:donkey");
        addInt(donkey, TagType::Byte, "ChestedHorse", 1);
        javaItem(add(donkey, TagType::List, "Items"), 2, "minecraft:bread", 3);
        Doc doc(std::move(chunk));

        InventoryLayout h = inventoryLayout(horse);
        check(h.kind == InventoryLayout::Kind::Mob && h.mount.size() == 2 && h.armor.empty() && h.hands.empty(),
              QStringLiteral("horse: saddle and armour, no helmet slots"));
        check(idOf(itemAt(horse, h.mount[0].ref)) == "minecraft:saddle" && h.mount[1].ref.name == "body_armor_item",
              QStringLiteral("  SaddleItem, body_armor_item (1.20.5+)"));
        check(InventoryEditor(doc.model.get(), h).put(h.mount[1].ref,
                                                      InventoryEditor(doc.model.get(), h).newItem("minecraft:diamond_horse_armor"),
                                                      QStringLiteral("put"))
                  && idOf(horse->child("body_armor_item")) == "minecraft:diamond_horse_armor",
              QStringLiteral("  diamond horse armour put on"));
        const InventoryLayout d = inventoryLayout(donkey);
        check(d.mount.size() == 1 && d.container.size() == 15 && d.columns == 5
                  && idOf(itemAt(donkey, d.container[0].ref)) == "minecraft:bread",
              QStringLiteral("donkey with a chest: saddle, 15 slots from number 2"));

        auto modern = std::make_unique<Tag>(TagType::Compound);
        addInt(modern.get(), TagType::Int, "DataVersion", 4440);
        addString(modern.get(), "id", "minecraft:llama");
        const InventoryLayout m = inventoryLayout(modern.get());
        check(m.mount.size() == 1 && m.mount[0].ref.container == "equipment" && m.mount[0].ref.name == "body",
              QStringLiteral("1.21.5 llama: equipment.body"));
    }

    {
        auto sword = std::make_unique<Tag>(TagType::Compound);
        addString(sword.get(), "id", "minecraft:diamond_sword");
        addInt(sword.get(), TagType::Int, "count", 1);
        {
            ItemEditorDialog dialog(*sword, false, 4440);
            dialog.applyForms();
            check(bytesOf(dialog.result().get()) == bytesOf(sword.get()),
                  QStringLiteral("item editor: nothing touched, nothing changed"));
            field<QLineEdit>(&dialog, "name")->setText(QStringLiteral("Excalibur"));
            field<QSpinBox>(&dialog, "durability")->setValue(1000);
            dialog.addEnchantment(QStringLiteral("minecraft:sharpness"), 5);
            field<QGroupBox>(&dialog, "food")->setChecked(true);
            field<QSpinBox>(&dialog, "food.nutrition")->setValue(4);
            field<QDoubleSpinBox>(&dialog, "food.saturation")->setValue(2.5);
            field<QGroupBox>(&dialog, "consumable")->setChecked(true);
            dialog.addConsumeEffect(QStringLiteral("minecraft:regeneration"), 2, 5, 0.5);
            field<QSpinBox>(&dialog, "max_stack_size")->setValue(16);
            field<QCheckBox>(&dialog, "unbreakable")->setChecked(true);
            dialog.applyForms();
            auto out = dialog.result();
            const Tag* c = out->child("components");
            check(c && c->child("minecraft:custom_name") && c->child("minecraft:custom_name")->string == "Excalibur",
                  QStringLiteral("  1.21.5 name: plain text"));
            check(c->child("minecraft:damage") && c->child("minecraft:damage")->integer == 561,
                  QStringLiteral("  1000 of 1561 durability: damage 561"));
            check(c->child("minecraft:enchantments") && c->child("minecraft:enchantments")->child("minecraft:sharpness")
                      && c->child("minecraft:enchantments")->child("minecraft:sharpness")->integer == 5,
                  QStringLiteral("  sharpness V"));
            const Tag* food = c->child("minecraft:food");
            check(food && food->child("nutrition")->integer == 4 && food->child("saturation")->floating == 2.5,
                  QStringLiteral("  food: 4 nutrition, 2.5 saturation"));
            const Tag* consumable = c->child("minecraft:consumable");
            const Tag* effects = consumable ? consumable->child("on_consume_effects") : nullptr;
            check(effects && effects->children.size() == 1
                      && effects->children[0]->child("type")->string == "minecraft:apply_effects"
                      && effects->children[0]->child("effects")->children[0]->child("id")->string == "minecraft:regeneration"
                      && effects->children[0]->child("effects")->children[0]->child("amplifier")->integer == 1
                      && effects->children[0]->child("effects")->children[0]->child("duration")->integer == 100
                      && std::abs(effects->children[0]->child("probability")->floating - 0.5) < 1e-6,
                  QStringLiteral("  consumable: regeneration II for 5 s, half the time"));
            check(c->child("minecraft:max_stack_size")->integer == 16 && c->child("minecraft:unbreakable"),
                  QStringLiteral("  stack of 16, unbreakable"));

            dialog.tabs()->setCurrentIndex(dialog.tabs()->count() - 1);
            dialog.tabs()->setCurrentIndex(0);
            check(field<QLineEdit>(&dialog, "name")->text() == "Excalibur"
                      && field<QSpinBox>(&dialog, "food.nutrition")->value() == 4,
                  QStringLiteral("  through the NBT tab and back"));
            field<QGroupBox>(&dialog, "food")->setChecked(false);
            dialog.applyForms();
            check(!dialog.result()->child("components")->child("minecraft:food"), QStringLiteral("  food taken away"));
        }
        {
            auto old = std::make_unique<Tag>(TagType::Compound);
            addString(old.get(), "id", "minecraft:iron_pickaxe");
            addInt(old.get(), TagType::Byte, "Count", 1);
            ItemEditorDialog dialog(*old, false, 3465);
            check(dialog.format() == ItemEditorDialog::Format::JavaLegacy, QStringLiteral("1.20.1 item: the old format"));
            field<QLineEdit>(&dialog, "name")->setText(QStringLiteral("Digger"));
            dialog.addEnchantment(QStringLiteral("minecraft:efficiency"), 4);
            dialog.applyForms();
            auto out = dialog.result();
            const Tag* tag = out->child("tag");
            check(tag && tag->child("display") && QString::fromStdString(tag->child("display")->child("Name")->string).contains("\"Digger\""),
                  QStringLiteral("  the name as JSON in tag.display.Name"));
            const Tag* ench = tag->child("Enchantments");
            check(ench && ench->children[0]->child("id")->string == "minecraft:efficiency"
                      && ench->children[0]->child("lvl")->type == TagType::Short,
                  QStringLiteral("  Enchantments [{id, lvl: short}]"));
        }
        {
            auto be = std::make_unique<Tag>(TagType::Compound);
            addInt(be.get(), TagType::Byte, "Count", 1);
            addInt(be.get(), TagType::Short, "Damage", 0);
            addString(be.get(), "Name", "minecraft:iron_sword");
            addInt(be.get(), TagType::Byte, "WasPickedUp", 0);
            ItemEditorDialog dialog(*be, true, 0);
            dialog.addEnchantment(QStringLiteral("minecraft:sharpness"), 3);
            field<QLineEdit>(&dialog, "name")->setText(QStringLiteral("Blade"));
            dialog.applyForms();
            auto out = dialog.result();
            const Tag* ench = out->child("tag") ? out->child("tag")->child("ench") : nullptr;
            check(ench && ench->children[0]->child("id")->integer == 9 && ench->children[0]->child("lvl")->integer == 3,
                  QStringLiteral("Bedrock item: tag.ench [{id: 9, lvl: 3}]"));
            check(out->child("tag")->child("display")->child("Name")->string == "Blade",
                  QStringLiteral("  the name as plain text"));
        }
    }

    {
        {
            auto be = std::make_unique<Tag>(TagType::Compound);
            addInt(be.get(), TagType::Byte, "Count", 1);
            addString(be.get(), "Name", "minecraft:diamond_sword");
            ItemEditorDialog bedrockDialog(*be, true, 0);
            auto modernSword = std::make_unique<Tag>(TagType::Compound);
            addString(modernSword.get(), "id", "minecraft:diamond_sword");
            addInt(modernSword.get(), TagType::Int, "count", 1);
            ItemEditorDialog modernDialog(*modernSword, false, 4440);
            check(bedrockDialog.enchantmentLevelLimit() == 32767 && modernDialog.enchantmentLevelLimit() == 255,
                  QStringLiteral("enchantment levels: Bedrock up to 32767, Java 1.20.5+ up to 255"));
            bedrockDialog.addEnchantment(QStringLiteral("minecraft:sharpness"), 32767);
            bedrockDialog.applyForms();
            auto bedrockOut = bedrockDialog.result();
            const Tag* ench = bedrockOut->child("tag")->child("ench");
            check(ench && ench->children[0]->child("lvl")->integer == 32767, QStringLiteral("  sharpness 32767 on Bedrock"));
        }
        {
            auto totem = std::make_unique<Tag>(TagType::Compound);
            addString(totem.get(), "id", "minecraft:stick");
            addInt(totem.get(), TagType::Int, "count", 1);
            ItemEditorDialog dialog(*totem, false, 4440);
            field<QLineEdit>(&dialog, "item_name")->setText(QStringLiteral("Lucky stick"));
            field<QGroupBox>(&dialog, "death_prevention")->setChecked(true);
            field<QCheckBox>(&dialog, "death_prevention.clear")->setChecked(true);
            dialog.addDeathEffect(QStringLiteral("minecraft:regeneration"), 2, 45);
            dialog.addAttribute(QStringLiteral("attack_damage"), 7, QStringLiteral("add_value"), QStringLiteral("mainhand"));
            field<QGroupBox>(&dialog, "equippable")->setChecked(true);
            field<QLineEdit>(&dialog, "equippable.slot")->setText(QStringLiteral("head"));
            field<QCheckBox>(&dialog, "hideTooltip")->setChecked(true);
            dialog.applyForms();
            auto out = dialog.result();
            const Tag* c = out->child("components");
            check(c->child("minecraft:item_name") && c->child("minecraft:item_name")->string == "Lucky stick",
                  QStringLiteral("item_name: a name without italics"));
            const Tag* death = c->child("minecraft:death_prevention");
            const Tag* effects = death ? death->child("death_effects") : nullptr;
            check(effects && effects->children.size() == 2
                      && effects->children[0]->child("type")->string == "minecraft:clear_all_effects"
                      && effects->children[1]->child("effects")->children[0]->child("id")->string == "minecraft:regeneration",
                  QStringLiteral("death_prevention: clear effects, then regeneration II"));
            const Tag* attributes = c->child("minecraft:attribute_modifiers");
            check(attributes && attributes->type == TagType::List && attributes->children[0]->child("type")->string
                                                                          == "minecraft:attack_damage"
                      && attributes->children[0]->child("amount")->floating == 7
                      && attributes->children[0]->child("id"),
                  QStringLiteral("attribute_modifiers (1.21.5): a list, attack_damage +7 with an id"));
            check(c->child("minecraft:equippable") && c->child("minecraft:equippable")->child("slot")->string == "head",
                  QStringLiteral("equippable: worn on the head"));
            check(c->child("minecraft:tooltip_display") && c->child("minecraft:tooltip_display")->child("hide_tooltip")->integer == 1,
                  QStringLiteral("tooltip_display.hide_tooltip (1.21.5)"));

            ItemEditorDialog old(*totem, false, 3955);
            old.addAttribute(QStringLiteral("block_interaction_range"), 2, QStringLiteral("add_value"), QStringLiteral("any"));
            old.applyForms();
            auto oldOut = old.result();
            const Tag* oldAttr = oldOut->child("components")->child("minecraft:attribute_modifiers");
            check(oldAttr && oldAttr->type == TagType::Compound && oldAttr->child("modifiers")->children[0]->child("type")->string
                                                                        == "minecraft:player.block_interaction_range",
                  QStringLiteral("  1.21.1: { modifiers: [...] } with player.block_interaction_range"));
        }
    }

    {
        auto file = std::make_unique<NbtFile>();
        file->root = oldJavaPlayer();
        NbtEditor editor(std::move(file), QString(), QString());
        editor.resize(1200, 800);
        editor.setAttribute(Qt::WA_DontShowOnScreen, true);
        editor.show();
        QApplication::processEvents();
        auto* tree = editor.findChild<QTreeView*>();
        const QModelIndex before = tree->currentIndex();
        auto cells = editor.inventoryPanel()->slotWidgets();
        const int hotbar0 = 4 + 1 + 27;
        emit cells[size_t(hotbar0)]->dropped(hotbar0, hotbar0 + 3);
        QApplication::processEvents();
        check(tree->currentIndex() == before && cells[size_t(hotbar0 + 3)]->itemId() == "minecraft:diamond_sword",
              QStringLiteral("a move in the panel leaves the tree where it was"));
        editor.undoStack()->undo();
        QApplication::processEvents();
        check(cells[size_t(hotbar0)]->itemId() == "minecraft:diamond_sword", QStringLiteral("  and is undone as usual"));
    }

    {
        PipBar hearts(PipBar::Kind::Hearts);
        hearts.setMaximum(20);
        hearts.setValue(20);
        const int step = 16;
        check(hearts.valueAt(QPoint(4 * step + 12, 5)) == 10, QStringLiteral("hearts: the right half of the 5th: 10"));
        check(hearts.valueAt(QPoint(6 * step + 3, 5)) == 13, QStringLiteral("  the left half of the 7th: 6.5 hearts"));
        check(hearts.valueAt(QPoint(9 * step + 17, 5)) == 20, QStringLiteral("  the last one: all 20"));
        hearts.setMaximum(40);
        check(hearts.sizeHint().height() > 30 && hearts.valueAt(QPoint(2, 25)) == 21,
              QStringLiteral("  40 health: a second row"));

        PipBar food(PipBar::Kind::Food);
        food.setValue(20);
        const int w = food.sizeHint().width();
        check(food.valueAt(QPoint(w - 3, 5)) == 1, QStringLiteral("food fills from the right: the outer half is 1"));
        check(food.valueAt(QPoint(w - 1 - (2 * step + 12), 5)) == 6, QStringLiteral("  the inner half of the 3rd: 6"));
        food.setValue(1);
        check(food.valueAt(QPoint(w - 3, 5)) == 0, QStringLiteral("  clicking the last half again: empty"));
    }

    {
        Doc doc(oldJavaPlayer());
        InventoryPanel panel;
        panel.resize(380, 900);
        panel.setAttribute(Qt::WA_DontShowOnScreen, true);
        panel.show();
        int shown = -1;
        QObject::connect(&panel, &InventoryPanel::holderChanged, [&](bool on) { shown = on; });
        Tag* revealed = nullptr;
        QObject::connect(&panel, &InventoryPanel::revealTag, [&](Tag* t) { revealed = t; });
        panel.setModel(doc.model.get());
        panel.showHolder(doc.root.get());
        QApplication::processEvents();
        const auto cells = panel.slotWidgets();
        check(shown == 1 && cells.size() == 4 + 1 + 27 + 9 + 27, QStringLiteral("panel: 68 slots for a Java player"));
        const int hotbar0 = 4 + 1 + 27;
        check(cells[size_t(hotbar0)]->itemId() == "minecraft:diamond_sword" && cells[0]->itemId() == "minecraft:iron_helmet",
              QStringLiteral("  the sword in the hotbar, the helmet on top"));
        check(panel.hearts() && panel.hearts()->value() == 17 && panel.food() && panel.food()->value() == 20,
              QStringLiteral("  17 health, full food"));

        emit cells[size_t(hotbar0)]->clicked(hotbar0);
        check(revealed && idOf(revealed) == "minecraft:diamond_sword", QStringLiteral("  a click shows the item in the tree"));

        emit cells[size_t(hotbar0)]->dropped(hotbar0, hotbar0 + 8);
        QApplication::processEvents();
        check(cells[size_t(hotbar0)]->isEmpty() && cells[size_t(hotbar0 + 8)]->itemId() == "minecraft:diamond_sword",
              QStringLiteral("  dragging to hotbar 9 moves it"));
        doc.undo.undo();
        QApplication::processEvents();
        check(cells[size_t(hotbar0)]->itemId() == "minecraft:diamond_sword" && cells[size_t(hotbar0 + 8)]->isEmpty(),
              QStringLiteral("  Ctrl+Z: the panel follows"));

        emit panel.hearts()->valueChosen(9);
        QApplication::processEvents();
        check(doc.root->child("Health")->floating == 9 && panel.hearts()->value() == 9,
              QStringLiteral("  clicking the hearts writes Health"));

        if (argc >= 2) {
            doc.undo.undo();
            for (int i = 0; i < 3; ++i)
                QApplication::processEvents();
            check(panel.grab().save(QString::fromLocal8Bit(argv[1])), QStringLiteral("picture saved"));
        }

        auto chunkLike = std::make_unique<Tag>(TagType::Compound);
        Doc outer(std::move(chunkLike));
        Tag* holderParent = add(outer.root.get(), TagType::List, "players");
        holderParent->append(oldJavaPlayer());
        panel.setModel(outer.model.get());
        panel.showHolder(holderParent->children[0].get());
        QApplication::processEvents();
        check(panel.holder() != nullptr, QStringLiteral("  a player inside a list"));
        outer.model->removeTag(outer.model->indexFromTag(holderParent->children[0].get()));
        QApplication::processEvents();
        check(!panel.holder() && shown == 0, QStringLiteral("  deleting it in the tree empties the panel"));
        outer.undo.clear();
        QApplication::processEvents();
        panel.setModel(nullptr);
    }

    if (argc >= 2) {
        const QString base = QString::fromLocal8Bit(argv[1]).chopped(4);
        auto grab = [](QWidget* w, const QString& file) {
            w->setAttribute(Qt::WA_DontShowOnScreen, true);
            w->show();
            for (int i = 0; i < 3; ++i)
                QApplication::processEvents();
            return w->grab().save(file);
        };
        auto apple = std::make_unique<Tag>(TagType::Compound);
        addString(apple.get(), "id", "minecraft:golden_apple");
        addInt(apple.get(), TagType::Int, "count", 3);
        ItemEditorDialog dialog(*apple, false, 4440);
        field<QLineEdit>(&dialog, "name")->setText(QStringLiteral("Яблоко бодрости"));
        dialog.addEnchantment(QStringLiteral("minecraft:unbreaking"), 3);
        field<QGroupBox>(&dialog, "food")->setChecked(true);
        field<QSpinBox>(&dialog, "food.nutrition")->setValue(4);
        field<QDoubleSpinBox>(&dialog, "food.saturation")->setValue(9.6);
        field<QGroupBox>(&dialog, "consumable")->setChecked(true);
        dialog.addConsumeEffect(QStringLiteral("minecraft:regeneration"), 2, 5, 1);
        dialog.addConsumeEffect(QStringLiteral("minecraft:absorption"), 1, 120, 1);
        dialog.resize(620, 640);
        bool saved = true;
        for (int t = 0; t < dialog.tabs()->count(); ++t) {
            dialog.tabs()->setCurrentIndex(t);
            saved = grab(&dialog, QStringLiteral("%1_item%2.png").arg(base).arg(t)) && saved;
        }
        dialog.hide();

        Doc doc(newJavaPlayer());
        Effect speed;
        speed.id = QStringLiteral("minecraft:speed");
        speed.amplifier = 1;
        speed.duration = 90 * 20;
        InventoryEditor(doc.model.get(), inventoryLayout(doc.root.get())).setEffect(-1, speed);
        speed.id = QStringLiteral("minecraft:night_vision");
        speed.amplifier = 0;
        speed.duration = -1;
        InventoryEditor(doc.model.get(), inventoryLayout(doc.root.get())).setEffect(-1, speed);
        InventoryPanel panel;
        panel.resize(380, 1000);
        panel.setModel(doc.model.get());
        panel.showHolder(doc.root.get());
        saved = grab(&panel, base + QStringLiteral("_effects.png")) && saved;
        panel.hide();

        {
            auto file = std::make_unique<NbtFile>();
            file->root = oldJavaPlayer();
            NbtEditor editor(std::move(file), QString(), QString());
            editor.resize(900, 300);
            auto* tree = editor.findChild<QTreeView*>();
            auto* model = qobject_cast<NbtModel*>(tree->model());
            Tag* health = model->rootTag()->child("Health");
            Tag value(TagType::Float);
            value.floating = 5;
            model->replaceValue(health, value);
            tree->setCurrentIndex(model->indexFromTag(health));
            saved = grab(&editor, base + QStringLiteral("_selected.png")) && saved;
        }

        MinecraftSourceDialog source;
        saved = grab(&source, base + QStringLiteral("_source.png")) && saved;
        check(saved, QStringLiteral("pictures of the item editor, effects and the Minecraft folder"));
    }

    std::printf("\n%d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
