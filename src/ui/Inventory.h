#pragma once

#include "core/Nbt.h"

#include <QCoreApplication>
#include <QString>

#include <memory>
#include <string>
#include <vector>

class NbtModel;

namespace justnbt {
struct SlotRef {
    enum class Place { Keyed, Indexed, Named, Wrapped };
    Place place = Place::Keyed;
    std::string container;
    int key = 0;
    std::string name;

    bool operator==(const SlotRef& o) const
    {
        return place == o.place && container == o.container && key == o.key && name == o.name;
    }
};

enum class SlotHint { None, Helmet, Chestplate, Leggings, Boots, Shield, Sword, Saddle, HorseArmor, LlamaArmor };

struct InventorySlot {
    SlotRef ref;
    SlotHint hint = SlotHint::None;
};

struct InventoryLayout {
    enum class Kind { None, Player, Mob, Container };
    Kind kind = Kind::None;
    bool bedrock = false;
    nbt::Tag* holder = nullptr;
    QString id;

    std::vector<InventorySlot> armor;
    std::vector<InventorySlot> hands;
    std::vector<InventorySlot> mount;
    std::vector<InventorySlot> storage;
    std::vector<InventorySlot> hotbar;
    std::vector<InventorySlot> ender;
    std::vector<InventorySlot> container;
    int columns = 9;

    bool valid() const { return kind != Kind::None; }
};

int dataVersionOf(const nbt::Tag* tag);

nbt::Tag* findInventoryHolder(nbt::Tag* tag);
nbt::Tag* levelDatPlayer(nbt::Tag* root);
InventoryLayout inventoryLayout(nbt::Tag* holder);

nbt::Tag* itemAt(const nbt::Tag* holder, const SlotRef& slot);

nbt::Tag* contentsHolder(nbt::Tag* item);
nbt::Tag* itemOfHolder(nbt::Tag* holder);

struct Effect {
    QString id;
    int amplifier = 0;
    int duration = 0;
    bool particles = true;
    bool ambient = false;
    nbt::Tag* tag = nullptr;
};
std::vector<Effect> activeEffects(nbt::Tag* holder, bool bedrock);
QStringList knownEffects(bool bedrock);

struct PlayerStats {
    nbt::Tag* health = nullptr;
    double maxHealth = 20;
    nbt::Tag* food = nullptr;
    nbt::Tag* saturation = nullptr;
    nbt::Tag* xpLevel = nullptr;
    nbt::Tag* xpProgress = nullptr;
    nbt::Tag* selectedSlot = nullptr;
    nbt::Tag* gameMode = nullptr;
};
PlayerStats playerStats(nbt::Tag* holder, bool bedrock);

class InventoryEditor {
    Q_DECLARE_TR_FUNCTIONS(InventoryEditor)

public:
    InventoryEditor(NbtModel* model, const InventoryLayout& layout) : model_(model), layout_(layout) {}

    bool move(const SlotRef& from, const SlotRef& to);
    bool clear(const SlotRef& slot);
    bool put(const SlotRef& slot, std::unique_ptr<nbt::Tag> item, const QString& what);
    std::unique_ptr<nbt::Tag> newItem(const QString& id) const;
    bool setCount(const SlotRef& slot, int count);
    bool setNumber(nbt::Tag* tag, double value);
    bool setEffect(int index, const Effect& effect);
    bool removeEffect(int index);

private:
    void clearRaw(const SlotRef& slot);
    void putRaw(const SlotRef& slot, std::unique_ptr<nbt::Tag> item);
    nbt::Tag* ensureContainer(const SlotRef& slot);
    std::unique_ptr<nbt::Tag> placeholder(const SlotRef& slot) const;

    NbtModel* model_;
    InventoryLayout layout_;
};
}
