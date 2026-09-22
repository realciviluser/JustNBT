#include "ui/Inventory.h"

#include "core/ItemInfo.h"
#include "ui/NbtModel.h"

#include <QUndoStack>

#include <algorithm>
#include <cmath>

namespace justnbt {
using nbt::Tag;
using nbt::TagType;

namespace {
Tag* listChild(const Tag* tag, const char* name)
{
    Tag* t = tag->child(name);
    return t && t->type == TagType::List ? t : nullptr;
}

Tag* compoundChild(const Tag* tag, const char* name)
{
    Tag* t = tag->child(name);
    return t && t->type == TagType::Compound ? t : nullptr;
}

Tag* numberChild(const Tag* tag, const char* name)
{
    Tag* t = tag->child(name);
    return t && (nbt::isInteger(t->type) || nbt::isFloating(t->type)) ? t : nullptr;
}

QString stringOf(const Tag* tag, const char* name)
{
    const Tag* t = tag->child(name);
    return t && t->type == TagType::String ? QString::fromStdString(t->string) : QString();
}

double numberOf(const Tag* t)
{
    return nbt::isFloating(t->type) ? t->floating : double(t->integer);
}

bool isPlayer(const Tag* t)
{
    if (!listChild(t, "Inventory"))
        return false;
    for (const char* marker : {"abilities", "foodLevel", "playerGameType", "EnderItems", "EnderChestInventory",
                               "PlayerGameMode", "XpLevel"})
        if (t->child(marker))
            return true;
    return false;
}

bool hasEquipment(const Tag* t)
{
    return listChild(t, "HandItems") || listChild(t, "ArmorItems") || listChild(t, "Mainhand")
           || listChild(t, "Armor") || compoundChild(t, "equipment");
}

QString plainId(QString id)
{
    const int colon = id.indexOf(':');
    if (colon >= 0)
        id = id.mid(colon + 1);
    QString out;
    for (int i = 0; i < id.size(); ++i) {
        const QChar c = id[i];
        if (c.isUpper() && i > 0 && id[i - 1].isLower())
            out += QLatin1Char('_');
        out += c.toLower();
    }
    return out;
}

QString holderId(const Tag* t)
{
    QString id = stringOf(t, "id");
    if (id.isEmpty())
        id = stringOf(t, "identifier");
    return id;
}

int containerSize(const QString& plain, int* columns)
{
    *columns = 9;
    if (plain.endsWith(QLatin1String("shulker_box")) || plain.endsWith(QLatin1String("chest_boat"))
        || plain.endsWith(QLatin1String("chest_raft")))
        return 27;
    static const struct {
        const char* id;
        int size, columns;
    } known[] = {{"chest", 27, 9},         {"trapped_chest", 27, 9}, {"barrel", 27, 9},
                 {"chest_minecart", 27, 9}, {"minecart_chest", 27, 9}, {"hopper", 5, 5},
                 {"hopper_minecart", 5, 5}, {"minecart_hopper", 5, 5}, {"dispenser", 9, 3},
                 {"dropper", 9, 3},         {"crafter", 9, 3},       {"furnace", 3, 3},
                 {"blast_furnace", 3, 3},   {"smoker", 3, 3},        {"brewing_stand", 5, 5},
                 {"chiseled_bookshelf", 6, 3}, {"campfire", 4, 4},   {"soul_campfire", 4, 4},
                 {"decorated_pot", 1, 1}};
    for (const auto& k : known)
        if (plain == QLatin1String(k.id)) {
            *columns = k.columns;
            return k.size;
        }
    return 0;
}

bool isEmptyItem(const Tag* t)
{
    if (!t || t->type != TagType::Compound || t->children.empty())
        return true;
    const Tag* id = t->child("id");
    if (!id || id->type != TagType::String)
        id = t->child("Name");
    if (!id || id->type != TagType::String || id->string.empty() || id->string == "minecraft:air"
        || id->string == "air")
        return true;
    const Tag* count = t->child("Count");
    if (!count)
        count = t->child("count");
    return count && nbt::isInteger(count->type) && count->integer <= 0;
}

int slotOf(const Tag* entry)
{
    const Tag* s = entry->child("Slot");
    return s && nbt::isInteger(s->type) ? int(s->integer) : -1000;
}

Tag* wrappedEntry(const Tag* list, int key)
{
    for (const auto& c : list->children)
        if (c->type == TagType::Compound)
            if (const Tag* s = c->child("slot"); s && nbt::isInteger(s->type) && s->integer == key)
                return c.get();
    return nullptr;
}

Tag* keyedEntry(const Tag* list, int key)
{
    for (const auto& c : list->children)
        if (c->type == TagType::Compound && slotOf(c.get()) == key)
            return c.get();
    return nullptr;
}

int dataVersion(const Tag* t)
{
    for (; t; t = t->parent)
        if (const Tag* v = t->child("DataVersion"); v && nbt::isInteger(v->type))
            return int(v->integer);
    return 0;
}

InventorySlot keyed(const char* container, int key, SlotHint hint = SlotHint::None)
{
    return {{SlotRef::Place::Keyed, container, key, {}}, hint};
}

InventorySlot indexed(const char* container, int key, SlotHint hint = SlotHint::None)
{
    return {{SlotRef::Place::Indexed, container, key, {}}, hint};
}

InventorySlot named(const char* name, SlotHint hint)
{
    return {{SlotRef::Place::Named, "equipment", 0, name}, hint};
}

void addRange(std::vector<InventorySlot>& to, const char* container, int first, int count)
{
    for (int i = 0; i < count; ++i)
        to.push_back(keyed(container, first + i));
}

bool usesEquipment(const Tag* holder)
{
    return compoundChild(holder, "equipment") || dataVersion(holder) >= 4325;
}

const char* const kJavaEffects[] = {
    "speed", "slowness", "haste", "mining_fatigue", "strength", "instant_health", "instant_damage", "jump_boost",
    "nausea", "regeneration", "resistance", "fire_resistance", "water_breathing", "invisibility", "blindness",
    "night_vision", "hunger", "weakness", "poison", "wither", "health_boost", "absorption", "saturation", "glowing",
    "levitation", "luck", "unluck", "slow_falling", "conduit_power", "dolphins_grace", "bad_omen",
    "hero_of_the_village", "darkness", "trial_omen", "raid_omen", "wind_charged", "weaving", "oozing", "infested",
    "breath_of_the_nautilus"};
const char* const kBedrockEffects[] = {
    "speed", "slowness", "haste", "mining_fatigue", "strength", "instant_health", "instant_damage", "jump_boost",
    "nausea", "regeneration", "resistance", "fire_resistance", "water_breathing", "invisibility", "blindness",
    "night_vision", "hunger", "weakness", "poison", "wither", "health_boost", "absorption", "saturation",
    "levitation", "fatal_poison", "conduit_power", "slow_falling", "bad_omen", "hero_of_the_village", "darkness",
    "trial_omen", "wind_charged", "weaving", "oozing", "infested", "raid_omen"};
constexpr int kJavaNumbered = 33;

bool modernEffects(const Tag* holder, bool bedrock)
{
    if (bedrock)
        return false;
    if (holder->child("active_effects"))
        return true;
    if (holder->child("ActiveEffects"))
        return false;
    return dataVersion(holder) >= 3578 || dataVersion(holder) == 0;
}
}

Tag* findInventoryHolder(Tag* tag)
{
    for (Tag* t = tag; t; t = t->parent) {
        if (t->type != TagType::Compound)
            continue;
        if (isPlayer(t) || listChild(t, "Items") || listChild(t, "minecraft:container")
            || (hasEquipment(t) && !holderId(t).isEmpty()))
            return t;
        int columns;
        if (containerSize(plainId(holderId(t)), &columns) > 0)
            return t;
    }
    return nullptr;
}

Tag* levelDatPlayer(Tag* root)
{
    if (!root)
        return nullptr;
    Tag* data = compoundChild(root, "Data");
    Tag* player = data ? compoundChild(data, "Player") : nullptr;
    return player && isPlayer(player) ? player : nullptr;
}

InventoryLayout inventoryLayout(Tag* holder)
{
    InventoryLayout l;
    if (!holder || holder->type != TagType::Compound)
        return l;
    l.holder = holder;
    l.id = holderId(holder);

    auto bedrockItems = [](const Tag* list) {
        if (!list)
            return false;
        for (const auto& c : list->children)
            if (c->type == TagType::Compound && c->child("Name") && c->child("WasPickedUp"))
                return true;
        return false;
    };
    l.bedrock = holder->child("identifier") || listChild(holder, "Mainhand") || listChild(holder, "Offhand")
                || holder->child("EnderChestInventory") || holder->child("PlayerGameMode")
                || bedrockItems(listChild(holder, "Items")) || bedrockItems(listChild(holder, "Inventory"));
    if (!l.bedrock && !l.id.isEmpty() && !l.id.contains(QLatin1Char(':')) && l.id.at(0).isUpper())
        l.bedrock = true;

    if (isPlayer(holder)) {
        l.kind = InventoryLayout::Kind::Player;
        if (l.bedrock) {
            l.armor = {indexed("Armor", 0, SlotHint::Helmet), indexed("Armor", 1, SlotHint::Chestplate),
                       indexed("Armor", 2, SlotHint::Leggings), indexed("Armor", 3, SlotHint::Boots)};
            l.hands = {indexed("Offhand", 0, SlotHint::Shield)};
            addRange(l.ender, "EnderChestInventory", 0, 27);
        } else {
            if (usesEquipment(holder)) {
                l.armor = {named("head", SlotHint::Helmet), named("chest", SlotHint::Chestplate),
                           named("legs", SlotHint::Leggings), named("feet", SlotHint::Boots)};
                l.hands = {named("offhand", SlotHint::Shield)};
            } else {
                l.armor = {keyed("Inventory", 103, SlotHint::Helmet), keyed("Inventory", 102, SlotHint::Chestplate),
                           keyed("Inventory", 101, SlotHint::Leggings), keyed("Inventory", 100, SlotHint::Boots)};
                l.hands = {keyed("Inventory", -106, SlotHint::Shield)};
            }
            addRange(l.ender, "EnderItems", 0, 27);
        }
        addRange(l.storage, "Inventory", 9, 27);
        addRange(l.hotbar, "Inventory", 0, 9);
        return l;
    }

    if (listChild(holder, "minecraft:container")) {
        l.kind = InventoryLayout::Kind::Container;
        const Tag* item = itemOfHolder(holder);
        int columns = 9;
        int size = item ? containerSize(plainId(stringOf(item, "id")), &columns) : 0;
        int highest = -1;
        for (const auto& c : listChild(holder, "minecraft:container")->children)
            if (const Tag* s = c->type == TagType::Compound ? numberChild(c.get(), "slot") : nullptr)
                highest = std::max(highest, int(s->integer));
        if (size == 0 || highest >= size) {
            columns = 9;
            size = std::max(27, (highest + 1 + 8) / 9 * 9);
        }
        l.columns = columns;
        for (int i = 0; i < size; ++i)
            l.container.push_back({{SlotRef::Place::Wrapped, "minecraft:container", i, {}}, SlotHint::None});
        return l;
    }

    const bool equipment = hasEquipment(holder) && !l.id.isEmpty();
    if (equipment) {
        l.kind = InventoryLayout::Kind::Mob;
        if (l.bedrock) {
            l.armor = {indexed("Armor", 0, SlotHint::Helmet), indexed("Armor", 1, SlotHint::Chestplate),
                       indexed("Armor", 2, SlotHint::Leggings), indexed("Armor", 3, SlotHint::Boots)};
            l.hands = {indexed("Mainhand", 0, SlotHint::Sword), indexed("Offhand", 0, SlotHint::Shield)};
        } else if (usesEquipment(holder)) {
            l.armor = {named("head", SlotHint::Helmet), named("chest", SlotHint::Chestplate),
                       named("legs", SlotHint::Leggings), named("feet", SlotHint::Boots)};
            l.hands = {named("mainhand", SlotHint::Sword), named("offhand", SlotHint::Shield)};
        } else {
            l.armor = {indexed("ArmorItems", 3, SlotHint::Helmet), indexed("ArmorItems", 2, SlotHint::Chestplate),
                       indexed("ArmorItems", 1, SlotHint::Leggings), indexed("ArmorItems", 0, SlotHint::Boots)};
            l.hands = {indexed("HandItems", 0, SlotHint::Sword), indexed("HandItems", 1, SlotHint::Shield)};
        }
    }

    const QString plain = plainId(l.id);
    const bool horse = plain == QLatin1String("horse") || plain == QLatin1String("donkey")
                       || plain == QLatin1String("mule") || plain == QLatin1String("skeleton_horse")
                       || plain == QLatin1String("zombie_horse") || plain == QLatin1String("camel")
                       || plain == QLatin1String("camel_husk");
    const bool llama = plain == QLatin1String("llama") || plain == QLatin1String("trader_llama");
    const bool wolf = plain == QLatin1String("wolf");
    if (!l.bedrock && (horse || llama || wolf)) {
        const bool modern = usesEquipment(holder);
        const bool components = dataVersion(holder) >= 3837;
        auto direct = [](const char* name, SlotHint hint) {
            return InventorySlot{{SlotRef::Place::Named, {}, 0, name}, hint};
        };
        if (horse)
            l.mount.push_back(modern ? named("saddle", SlotHint::Saddle) : direct("SaddleItem", SlotHint::Saddle));
        const bool armoured = plain == QLatin1String("horse") || llama || wolf;
        if (armoured) {
            const SlotHint hint = llama ? SlotHint::LlamaArmor : wolf ? SlotHint::None : SlotHint::HorseArmor;
            if (modern)
                l.mount.push_back(named("body", hint));
            else if (components || holder->child("body_armor_item"))
                l.mount.push_back(direct("body_armor_item", hint));
            else
                l.mount.push_back(direct(llama ? "DecorItem" : "ArmorItem", hint));
        }
        l.armor.clear();
        l.hands.clear();
        if (l.kind == InventoryLayout::Kind::None)
            l.kind = InventoryLayout::Kind::Mob;
    }
    const Tag* chested = holder->child("ChestedHorse");
    if (!l.bedrock && chested && nbt::isInteger(chested->type) && chested->integer) {
        int size = 15;
        if (llama)
            if (const Tag* strength = numberChild(holder, "Strength"))
                size = std::clamp(int(strength->integer), 1, 5) * 3;
        l.columns = llama ? size / 3 : 5;
        addRange(l.container, "Items", 2, size);
        return l;
    }

    const Tag* items = listChild(holder, "Items");
    int columns = 9;
    int size = containerSize(plainId(l.id), &columns);
    if (size == 0)
        if (const Tag* item = itemOfHolder(holder)) {
            QString itemId = stringOf(item, "id");
            if (itemId.isEmpty())
                itemId = stringOf(item, "Name");
            size = containerSize(plainId(itemId), &columns);
            if (size == 0)
                size = 27;
        }
    if (items || size > 0) {
        if (!equipment)
            l.kind = InventoryLayout::Kind::Container;
        int highest = -1;
        if (items)
            for (const auto& c : items->children)
                if (c->type == TagType::Compound)
                    highest = std::max(highest, slotOf(c.get()));
        if (size == 0 || highest >= size) {
            columns = 9;
            size = std::max(9, (highest + 1 + 8) / 9 * 9);
        }
        l.columns = columns;
        addRange(l.container, "Items", 0, size);
    }
    return l;
}

Tag* itemAt(const Tag* holder, const SlotRef& slot)
{
    if (!holder)
        return nullptr;
    const Tag* c = slot.container.empty() ? holder : holder->child(slot.container);
    if (!c)
        return nullptr;
    Tag* item = nullptr;
    switch (slot.place) {
    case SlotRef::Place::Keyed:
        if (c->type == TagType::List)
            item = keyedEntry(c, slot.key);
        break;
    case SlotRef::Place::Indexed:
        if (c->type == TagType::List && slot.key >= 0 && slot.key < int(c->children.size()))
            item = c->children[size_t(slot.key)].get();
        break;
    case SlotRef::Place::Named:
        if (c->type == TagType::Compound)
            item = c->child(slot.name);
        break;
    case SlotRef::Place::Wrapped:
        if (Tag* entry = c->type == TagType::List ? wrappedEntry(c, slot.key) : nullptr)
            item = entry->child("item");
        break;
    }
    return isEmptyItem(item) ? nullptr : item;
}

Tag* contentsHolder(Tag* item)
{
    if (!item || item->type != TagType::Compound)
        return nullptr;
    if (Tag* components = compoundChild(item, "components"); components && listChild(components, "minecraft:container"))
        return components;
    if (Tag* tag = compoundChild(item, "tag")) {
        if (Tag* blockEntity = compoundChild(tag, "BlockEntityTag"); blockEntity && listChild(blockEntity, "Items"))
            return blockEntity;
        if (listChild(tag, "Items"))
            return tag;
    }
    return nullptr;
}

Tag* itemOfHolder(Tag* holder)
{
    if (!holder)
        return nullptr;
    Tag* up = holder->parent;
    if (holder->name == "BlockEntityTag" && up)
        up = up->parent;
    if (up && up->type == TagType::Compound && (holder->name == "components" || holder->name == "tag"
                                                || holder->name == "BlockEntityTag")
        && (up->child("id") || up->child("Name")))
        return up;
    return nullptr;
}

PlayerStats playerStats(Tag* holder, bool bedrock)
{
    PlayerStats s;
    if (!holder)
        return s;
    if (bedrock) {
        if (Tag* attributes = listChild(holder, "Attributes")) {
            for (const auto& a : attributes->children) {
                if (a->type != TagType::Compound)
                    continue;
                const QString name = stringOf(a.get(), "Name");
                Tag* current = numberChild(a.get(), "Current");
                if (name == QLatin1String("minecraft:health")) {
                    s.health = current;
                    if (const Tag* max = numberChild(a.get(), "Max"))
                        s.maxHealth = numberOf(max);
                } else if (name == QLatin1String("minecraft:player.hunger")) {
                    s.food = current;
                } else if (name == QLatin1String("minecraft:player.saturation")) {
                    s.saturation = current;
                } else if (name == QLatin1String("minecraft:player.level")) {
                    s.xpLevel = current;
                } else if (name == QLatin1String("minecraft:player.experience")) {
                    s.xpProgress = current;
                }
            }
        }
        s.selectedSlot = numberChild(holder, "SelectedInventorySlot");
        s.gameMode = numberChild(holder, "PlayerGameMode");
        return s;
    }
    s.health = numberChild(holder, "Health");
    s.food = numberChild(holder, "foodLevel");
    s.saturation = numberChild(holder, "foodSaturationLevel");
    s.xpLevel = numberChild(holder, "XpLevel");
    s.xpProgress = numberChild(holder, "XpP");
    s.selectedSlot = numberChild(holder, "SelectedItemSlot");
    s.gameMode = numberChild(holder, "playerGameType");
    for (const char* list : {"attributes", "Attributes"}) {
        const Tag* attributes = listChild(holder, list);
        if (!attributes)
            continue;
        for (const auto& a : attributes->children) {
            if (a->type != TagType::Compound)
                continue;
            QString name = stringOf(a.get(), "id");
            if (name.isEmpty())
                name = stringOf(a.get(), "Name");
            if (!name.contains(QLatin1String("max_health")) && !name.contains(QLatin1String("maxHealth")))
                continue;
            const Tag* base = numberChild(a.get(), "base");
            if (!base)
                base = numberChild(a.get(), "Base");
            if (base && numberOf(base) > 0)
                s.maxHealth = numberOf(base);
        }
    }
    return s;
}

QStringList knownEffects(bool bedrock)
{
    QStringList ids;
    if (bedrock)
        for (const char* e : kBedrockEffects)
            ids << QStringLiteral("minecraft:") + QLatin1String(e);
    else
        for (const char* e : kJavaEffects)
            ids << QStringLiteral("minecraft:") + QLatin1String(e);
    return ids;
}

std::vector<Effect> activeEffects(Tag* holder, bool bedrock)
{
    std::vector<Effect> out;
    if (!holder)
        return out;
    const bool modern = modernEffects(holder, bedrock);
    Tag* list = listChild(holder, modern ? "active_effects" : "ActiveEffects");
    if (!list)
        return out;
    for (const auto& c : list->children) {
        if (c->type != TagType::Compound)
            continue;
        Effect e;
        e.tag = c.get();
        if (modern) {
            e.id = stringOf(c.get(), "id");
            if (const Tag* t = numberChild(c.get(), "amplifier"))
                e.amplifier = int(t->integer);
            if (const Tag* t = numberChild(c.get(), "duration"))
                e.duration = int(t->integer);
            if (const Tag* t = numberChild(c.get(), "show_particles"))
                e.particles = t->integer != 0;
            if (const Tag* t = numberChild(c.get(), "ambient"))
                e.ambient = t->integer != 0;
        } else {
            const Tag* id = numberChild(c.get(), "Id");
            const int n = id ? int(id->integer) : 0;
            const int count = bedrock ? int(std::size(kBedrockEffects)) : kJavaNumbered;
            e.id = n >= 1 && n <= count ? QStringLiteral("minecraft:") + QLatin1String((bedrock ? kBedrockEffects : kJavaEffects)[n - 1])
                                        : QString::number(n);
            if (const Tag* t = numberChild(c.get(), "Amplifier"))
                e.amplifier = int(t->integer);
            if (const Tag* t = numberChild(c.get(), "Duration"))
                e.duration = int(t->integer);
            if (const Tag* t = numberChild(c.get(), "ShowParticles"))
                e.particles = t->integer != 0;
            if (const Tag* t = numberChild(c.get(), "Ambient"))
                e.ambient = t->integer != 0;
        }
        out.push_back(e);
    }
    return out;
}

std::unique_ptr<Tag> InventoryEditor::placeholder(const SlotRef& slot) const
{
    auto t = std::make_unique<Tag>(TagType::Compound);
    if (!layout_.bedrock)
        return t;
    auto add = [&t](TagType type, const char* name) { return t->append(std::make_unique<Tag>(type, name)); };
    add(TagType::Byte, "Count");
    add(TagType::Short, "Damage");
    add(TagType::String, "Name");
    add(TagType::Byte, "WasPickedUp");
    if (slot.place == SlotRef::Place::Keyed)
        add(TagType::Byte, "Slot")->integer = slot.key;
    return t;
}

Tag* InventoryEditor::ensureContainer(const SlotRef& slot)
{
    Tag* holder = layout_.holder;
    if (slot.container.empty())
        return holder;
    if (Tag* c = holder->child(slot.container))
        return c;
    auto c = std::make_unique<Tag>(slot.place == SlotRef::Place::Named ? TagType::Compound : TagType::List,
                                   slot.container);
    if (c->type == TagType::List)
        c->listType = TagType::Compound;
    return model_->tagFromIndex(
        model_->insertTag(model_->indexFromTag(holder), int(holder->children.size()), std::move(c)));
}

void InventoryEditor::clearRaw(const SlotRef& slot)
{
    Tag* c = slot.container.empty() ? layout_.holder : layout_.holder->child(slot.container);
    if (!c)
        return;
    Tag* entry = nullptr;
    switch (slot.place) {
    case SlotRef::Place::Keyed:
        entry = c->type == TagType::List ? keyedEntry(c, slot.key) : nullptr;
        break;
    case SlotRef::Place::Indexed:
        if (c->type == TagType::List && slot.key < int(c->children.size()))
            entry = c->children[size_t(slot.key)].get();
        break;
    case SlotRef::Place::Named:
        entry = c->child(slot.name);
        break;
    case SlotRef::Place::Wrapped:
        entry = c->type == TagType::List ? wrappedEntry(c, slot.key) : nullptr;
        if (entry && isEmptyItem(entry->child("item")))
            entry = nullptr;
        break;
    }
    if (!entry || (slot.place != SlotRef::Place::Wrapped && isEmptyItem(entry)))
        return;
    const int row = c->indexOf(entry);
    model_->removeTag(model_->indexFromTag(entry));
    const bool keepPlace = slot.place == SlotRef::Place::Indexed
                           || (slot.place == SlotRef::Place::Keyed && layout_.bedrock);
    if (keepPlace)
        model_->insertTag(model_->indexFromTag(c), row, placeholder(slot));
}

void InventoryEditor::putRaw(const SlotRef& slot, std::unique_ptr<Tag> item)
{
    Tag* slotTag = item->child("Slot");
    if (slot.place == SlotRef::Place::Keyed) {
        if (!slotTag)
            slotTag = item->append(std::make_unique<Tag>(TagType::Byte, "Slot"));
        slotTag->type = TagType::Byte;
        slotTag->integer = slot.key;
    } else if (slotTag) {
        item->take(size_t(item->indexOf(slotTag)));
    }
    item->name = slot.place == SlotRef::Place::Named ? slot.name
                 : slot.place == SlotRef::Place::Wrapped ? std::string("item")
                                                        : std::string();

    Tag* c = ensureContainer(slot);
    if (!c)
        return;
    const QModelIndex parent = model_->indexFromTag(c);
    switch (slot.place) {
    case SlotRef::Place::Keyed: {
        if (Tag* old = keyedEntry(c, slot.key)) {
            const int row = c->indexOf(old);
            model_->removeTag(model_->indexFromTag(old));
            model_->insertTag(parent, row, std::move(item));
            return;
        }
        int row = int(c->children.size());
        if (!layout_.bedrock)
            for (int i = 0; i < int(c->children.size()); ++i)
                if (slotOf(c->children[size_t(i)].get()) > slot.key) {
                    row = i;
                    break;
                }
        model_->insertTag(parent, row, std::move(item));
        return;
    }
    case SlotRef::Place::Indexed:
        while (int(c->children.size()) < slot.key) {
            SlotRef filler = slot;
            filler.key = int(c->children.size());
            model_->insertTag(parent, filler.key, placeholder(filler));
        }
        if (slot.key < int(c->children.size()))
            model_->removeTag(model_->indexFromTag(c->children[size_t(slot.key)].get()));
        model_->insertTag(parent, slot.key, std::move(item));
        return;
    case SlotRef::Place::Named:
        if (Tag* old = c->child(slot.name))
            model_->removeTag(model_->indexFromTag(old));
        model_->insertTag(parent, int(c->children.size()), std::move(item));
        return;
    case SlotRef::Place::Wrapped: {
        if (Tag* old = wrappedEntry(c, slot.key))
            model_->removeTag(model_->indexFromTag(old));
        auto entry = std::make_unique<Tag>(TagType::Compound);
        entry->append(std::move(item));
        entry->append(std::make_unique<Tag>(TagType::Int, "slot"))->integer = slot.key;
        int row = int(c->children.size());
        for (int i = 0; i < int(c->children.size()); ++i)
            if (const Tag* s = c->children[size_t(i)]->child("slot"); s && s->integer > slot.key) {
                row = i;
                break;
            }
        model_->insertTag(parent, row, std::move(entry));
        return;
    }
    }
}

namespace {
QString itemName(const Tag* item)
{
    const auto stack = readItemStack(*item);
    return stack ? itemDisplayName(*stack) : QString();
}
}

bool InventoryEditor::move(const SlotRef& from, const SlotRef& to)
{
    if (from == to)
        return false;
    Tag* a = itemAt(layout_.holder, from);
    if (!a)
        return false;
    Tag* b = itemAt(layout_.holder, to);
    QUndoStack* undo = model_->undoStack();
    undo->beginMacro(tr("move %1").arg(itemName(a)));
    if (!layout_.bedrock && from.place == SlotRef::Place::Keyed && to.place == SlotRef::Place::Keyed
        && from.container == to.container) {
        Tag* slotA = a->child("Slot");
        Tag* slotB = b ? b->child("Slot") : nullptr;
        Tag value(TagType::Byte);
        copyValue(value, *slotA);
        value.integer = to.key;
        model_->replaceValue(slotA, value);
        if (slotB) {
            copyValue(value, *slotB);
            value.integer = from.key;
            model_->replaceValue(slotB, value);
        }
    } else {
        auto first = a->clone();
        auto second = b ? b->clone() : nullptr;
        clearRaw(from);
        clearRaw(to);
        putRaw(to, std::move(first));
        if (second)
            putRaw(from, std::move(second));
    }
    undo->endMacro();
    return true;
}

int dataVersionOf(const Tag* tag)
{
    return dataVersion(tag);
}

std::unique_ptr<Tag> InventoryEditor::newItem(const QString& id) const
{
    auto item = std::make_unique<Tag>(TagType::Compound);
    auto add = [&item](TagType type, const char* name) { return item->append(std::make_unique<Tag>(type, name)); };
    if (layout_.bedrock) {
        add(TagType::Byte, "Count")->integer = 1;
        add(TagType::Short, "Damage");
        add(TagType::String, "Name")->string = id.toStdString();
        add(TagType::Byte, "WasPickedUp");
    } else if (dataVersion(layout_.holder) >= 3837 || dataVersion(layout_.holder) == 0) {
        add(TagType::String, "id")->string = id.toStdString();
        add(TagType::Int, "count")->integer = 1;
    } else {
        add(TagType::String, "id")->string = id.toStdString();
        add(TagType::Byte, "Count")->integer = 1;
    }
    return item;
}

bool InventoryEditor::put(const SlotRef& slot, std::unique_ptr<Tag> item, const QString& what)
{
    QUndoStack* undo = model_->undoStack();
    undo->beginMacro(what);
    clearRaw(slot);
    putRaw(slot, std::move(item));
    undo->endMacro();
    return true;
}

bool InventoryEditor::clear(const SlotRef& slot)
{
    Tag* a = itemAt(layout_.holder, slot);
    if (!a)
        return false;
    QUndoStack* undo = model_->undoStack();
    undo->beginMacro(tr("remove %1").arg(itemName(a)));
    clearRaw(slot);
    undo->endMacro();
    return true;
}

bool InventoryEditor::setCount(const SlotRef& slot, int count)
{
    Tag* a = itemAt(layout_.holder, slot);
    if (!a)
        return false;
    Tag* c = numberChild(a, "Count");
    if (!c)
        c = numberChild(a, "count");
    if (!c) {
        if (count == 1)
            return false;
        auto added = std::make_unique<Tag>(TagType::Int, "count");
        added->integer = count;
        model_->insertTag(model_->indexFromTag(a), int(a->children.size()), std::move(added));
        return true;
    }
    return setNumber(c, count);
}

namespace {
void setField(Tag* compound, TagType type, const char* name, int64_t value)
{
    Tag* t = compound->child(name);
    if (!t)
        t = compound->append(std::make_unique<Tag>(type, name));
    t->integer = value;
}
}

bool InventoryEditor::setEffect(int index, const Effect& effect)
{
    Tag* holder = layout_.holder;
    const bool bedrock = layout_.bedrock;
    const bool modern = modernEffects(holder, bedrock);
    const auto current = activeEffects(holder, bedrock);
    if (index >= int(current.size()))
        return false;
    auto tag = index >= 0 ? current[size_t(index)].tag->clone() : std::make_unique<Tag>(TagType::Compound);
    tag->name.clear();
    if (modern) {
        Tag* id = tag->child("id");
        if (!id)
            id = tag->insert(0, std::make_unique<Tag>(TagType::String, "id"));
        id->string = effect.id.toStdString();
        setField(tag.get(), TagType::Byte, "amplifier", effect.amplifier);
        setField(tag.get(), TagType::Int, "duration", effect.duration);
        setField(tag.get(), TagType::Byte, "ambient", effect.ambient);
        setField(tag.get(), TagType::Byte, "show_particles", effect.particles);
        setField(tag.get(), TagType::Byte, "show_icon", 1);
    } else {
        const QStringList known = knownEffects(bedrock);
        const int number = int(known.indexOf(effect.id)) + 1;
        if (number <= 0 || (!bedrock && number > kJavaNumbered))
            return false;
        setField(tag.get(), TagType::Byte, "Id", number);
        setField(tag.get(), TagType::Byte, "Amplifier", effect.amplifier);
        setField(tag.get(), TagType::Int, "Duration", effect.duration);
        setField(tag.get(), TagType::Byte, "Ambient", effect.ambient);
        setField(tag.get(), TagType::Byte, "ShowParticles", effect.particles);
        if (bedrock) {
            for (const char* d : {"DurationEasy", "DurationNormal", "DurationHard"})
                setField(tag.get(), TagType::Int, d, effect.duration);
            setField(tag.get(), TagType::Byte, "DisplayOnScreenTextureAnimation", 0);
        } else {
            setField(tag.get(), TagType::Byte, "ShowIcon", 1);
        }
    }

    QUndoStack* undo = model_->undoStack();
    undo->beginMacro(index >= 0 ? tr("change an effect") : tr("add an effect"));
    SlotRef list{SlotRef::Place::Keyed, modern ? "active_effects" : "ActiveEffects", 0, {}};
    Tag* c = ensureContainer(list);
    if (index >= 0) {
        Tag* old = current[size_t(index)].tag;
        const int row = c->indexOf(old);
        model_->removeTag(model_->indexFromTag(old));
        model_->insertTag(model_->indexFromTag(c), row, std::move(tag));
    } else {
        model_->insertTag(model_->indexFromTag(c), int(c->children.size()), std::move(tag));
    }
    undo->endMacro();
    return true;
}

bool InventoryEditor::removeEffect(int index)
{
    const auto current = activeEffects(layout_.holder, layout_.bedrock);
    if (index < 0 || index >= int(current.size()))
        return false;
    QUndoStack* undo = model_->undoStack();
    undo->beginMacro(tr("remove an effect"));
    model_->removeTag(model_->indexFromTag(current[size_t(index)].tag));
    undo->endMacro();
    return true;
}

bool InventoryEditor::setNumber(Tag* tag, double value)
{
    if (!tag)
        return false;
    Tag v(tag->type);
    copyValue(v, *tag);
    if (nbt::isInteger(tag->type))
        v.integer = std::llround(value);
    else if (nbt::isFloating(tag->type))
        v.floating = tag->type == TagType::Float ? double(float(value)) : value;
    else
        return false;
    if (v.integer == tag->integer && v.floating == tag->floating)
        return false;
    model_->replaceValue(tag, v);
    return true;
}
}
