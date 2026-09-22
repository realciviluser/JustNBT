#include "ui/ItemEditorDialog.h"

#include "core/GameAssets.h"
#include "core/ItemInfo.h"
#include "ui/Dialogs.h"
#include "ui/Inventory.h"
#include "ui/ItemVisuals.h"
#include "ui/NbtModel.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QCompleter>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRandomGenerator>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTreeView>
#include <QUndoStack>
#include <QVBoxLayout>

#include <cmath>
#include <tuple>

using nbt::Tag;
using nbt::TagType;
using namespace justnbt;

namespace {
Tag* childOf(const Tag* parent, const char* name)
{
    return parent ? parent->child(name) : nullptr;
}

Tag* childOf(const Tag* parent, const QString& name)
{
    return parent ? parent->child(name.toStdString()) : nullptr;
}

Tag* ensureChild(Tag* parent, TagType type, const std::string& name)
{
    if (Tag* t = parent->child(name)) {
        if (t->type == type)
            return t;
        parent->take(size_t(parent->indexOf(t)));
    }
    auto t = std::make_unique<Tag>(type, name);
    if (type == TagType::List)
        t->listType = TagType::Compound;
    return parent->append(std::move(t));
}

void removeChild(Tag* parent, const std::string& name)
{
    if (!parent)
        return;
    if (Tag* t = parent->child(name))
        parent->take(size_t(parent->indexOf(t)));
}

void setInt(Tag* parent, TagType type, const std::string& name, int64_t value)
{
    ensureChild(parent, type, name)->integer = value;
}

void setFloat(Tag* parent, const std::string& name, double value)
{
    ensureChild(parent, TagType::Float, name)->floating = double(float(value));
}

void setString(Tag* parent, const std::string& name, const QString& value)
{
    ensureChild(parent, TagType::String, name)->string = value.toStdString();
}

double numberOf(const Tag* t, double fallback = 0)
{
    if (!t)
        return fallback;
    if (nbt::isFloating(t->type))
        return t->floating;
    if (nbt::isInteger(t->type))
        return double(t->integer);
    return fallback;
}

QString stringOf(const Tag* t)
{
    return t && t->type == TagType::String ? QString::fromStdString(t->string) : QString();
}

QString localName(const QString& kind, const QString& id)
{
    const int colon = id.indexOf(QLatin1Char(':'));
    const QString text = GameAssets::instance().translate(
        QStringLiteral("%1.%2.%3").arg(kind, colon >= 0 ? id.left(colon) : QStringLiteral("minecraft"), id.mid(colon + 1)));
    return text.isEmpty() ? prettifyId(id) : text;
}

QComboBox* idCombo(const QStringList& ids, const QString& kind, const QString& current)
{
    auto* combo = new QComboBox;
    combo->setEditable(true);
    combo->setInsertPolicy(QComboBox::NoInsert);
    for (const QString& id : ids)
        combo->addItem(localName(kind, id), id);
    int at = combo->findData(current);
    if (at < 0 && !current.isEmpty()) {
        combo->addItem(current, current);
        at = combo->count() - 1;
    }
    combo->setCurrentIndex(std::max(0, at));
    combo->completer()->setFilterMode(Qt::MatchContains);
    combo->completer()->setCompletionMode(QCompleter::PopupCompletion);
    return combo;
}

QString comboId(const QComboBox* combo)
{
    const int at = combo->findText(combo->currentText());
    if (at >= 0 && combo->itemData(at).isValid())
        return combo->itemData(at).toString();
    QString typed = combo->currentText().trimmed();
    if (!typed.isEmpty() && !typed.contains(QLatin1Char(':')) && !typed.startsWith(QLatin1Char('#')))
        typed.prepend(QStringLiteral("minecraft:"));
    return typed;
}

QString shortName(const QString& component)
{
    return component.mid(component.indexOf(QLatin1Char(':')) + 1);
}

void showColor(QPushButton* button, QColor color)
{
    QPixmap pm(40, 16);
    pm.fill(color);
    button->setIcon(QIcon(pm));
    button->setIconSize(pm.size());
    button->setText(color.name().toUpper());
    button->setProperty("color", color);
}

QPushButton* colorButton(QWidget* parent)
{
    auto* button = new QPushButton;
    showColor(button, QColor(0xa0, 0x65, 0x40));
    QObject::connect(button, &QPushButton::clicked, parent, [button, parent] {
        const QColor c = QColorDialog::getColor(button->property("color").value<QColor>(), parent);
        if (c.isValid())
            showColor(button, c);
    });
    return button;
}

const QStringList kPotions = {
    QStringLiteral("minecraft:water"), QStringLiteral("minecraft:awkward"), QStringLiteral("minecraft:mundane"),
    QStringLiteral("minecraft:thick"), QStringLiteral("minecraft:night_vision"), QStringLiteral("minecraft:long_night_vision"),
    QStringLiteral("minecraft:invisibility"), QStringLiteral("minecraft:long_invisibility"), QStringLiteral("minecraft:leaping"),
    QStringLiteral("minecraft:long_leaping"), QStringLiteral("minecraft:strong_leaping"), QStringLiteral("minecraft:fire_resistance"),
    QStringLiteral("minecraft:long_fire_resistance"), QStringLiteral("minecraft:swiftness"), QStringLiteral("minecraft:long_swiftness"),
    QStringLiteral("minecraft:strong_swiftness"), QStringLiteral("minecraft:slowness"), QStringLiteral("minecraft:long_slowness"),
    QStringLiteral("minecraft:strong_slowness"), QStringLiteral("minecraft:turtle_master"), QStringLiteral("minecraft:long_turtle_master"),
    QStringLiteral("minecraft:strong_turtle_master"), QStringLiteral("minecraft:water_breathing"), QStringLiteral("minecraft:long_water_breathing"),
    QStringLiteral("minecraft:healing"), QStringLiteral("minecraft:strong_healing"), QStringLiteral("minecraft:harming"),
    QStringLiteral("minecraft:strong_harming"), QStringLiteral("minecraft:poison"), QStringLiteral("minecraft:long_poison"),
    QStringLiteral("minecraft:strong_poison"), QStringLiteral("minecraft:regeneration"), QStringLiteral("minecraft:long_regeneration"),
    QStringLiteral("minecraft:strong_regeneration"), QStringLiteral("minecraft:strength"), QStringLiteral("minecraft:long_strength"),
    QStringLiteral("minecraft:strong_strength"), QStringLiteral("minecraft:weakness"), QStringLiteral("minecraft:long_weakness"),
    QStringLiteral("minecraft:luck"), QStringLiteral("minecraft:slow_falling"), QStringLiteral("minecraft:long_slow_falling"),
    QStringLiteral("minecraft:wind_charged"), QStringLiteral("minecraft:weaving"), QStringLiteral("minecraft:oozing"),
    QStringLiteral("minecraft:infested")};

const QStringList kAttributes = {
    QStringLiteral("attack_damage"), QStringLiteral("attack_speed"), QStringLiteral("attack_knockback"),
    QStringLiteral("max_health"), QStringLiteral("max_absorption"), QStringLiteral("armor"),
    QStringLiteral("armor_toughness"), QStringLiteral("knockback_resistance"), QStringLiteral("movement_speed"),
    QStringLiteral("movement_efficiency"), QStringLiteral("scale"), QStringLiteral("step_height"),
    QStringLiteral("jump_strength"), QStringLiteral("gravity"), QStringLiteral("safe_fall_distance"),
    QStringLiteral("fall_damage_multiplier"), QStringLiteral("luck"), QStringLiteral("oxygen_bonus"),
    QStringLiteral("water_movement_efficiency"), QStringLiteral("burning_time"),
    QStringLiteral("explosion_knockback_resistance"), QStringLiteral("block_interaction_range"),
    QStringLiteral("entity_interaction_range"), QStringLiteral("block_break_speed"),
    QStringLiteral("mining_efficiency"), QStringLiteral("submerged_mining_speed"), QStringLiteral("sneaking_speed"),
    QStringLiteral("sweeping_damage_ratio"), QStringLiteral("follow_range"), QStringLiteral("flying_speed")};
const QStringList kPlayerAttributes = {
    QStringLiteral("block_interaction_range"), QStringLiteral("entity_interaction_range"),
    QStringLiteral("block_break_speed"), QStringLiteral("mining_efficiency"),
    QStringLiteral("submerged_mining_speed"), QStringLiteral("sneaking_speed"), QStringLiteral("sweeping_damage_ratio")};
}

struct EffectRow {
    QString id;
    int level = 1;
    double seconds = 10;
    double chance = 1;
    bool operator==(const EffectRow& o) const
    {
        return id == o.id && level == o.level && seconds == o.seconds && chance == o.chance;
    }
};

class EffectsTable : public QWidget {
public:
    EffectsTable(bool withChance, QWidget* parent = nullptr) : QWidget(parent), chance_(withChance)
    {
        table_ = new QTableWidget(0, withChance ? 4 : 3);
        QStringList headers{ItemEditorDialog::tr("Effect"), ItemEditorDialog::tr("Level"), ItemEditorDialog::tr("Seconds")};
        if (withChance)
            headers << ItemEditorDialog::tr("Chance");
        table_->setHorizontalHeaderLabels(headers);
        table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        table_->verticalHeader()->hide();
        table_->setMinimumHeight(110);
        auto* add = new QPushButton(ItemEditorDialog::tr("Add an effect"));
        auto* remove = new QPushButton(ItemEditorDialog::tr("Remove"));
        connect(add, &QPushButton::clicked, this, [this] { addRow({QStringLiteral("minecraft:regeneration"), 1, 10, 1}); });
        connect(remove, &QPushButton::clicked, this, [this] {
            if (table_->currentRow() >= 0)
                table_->removeRow(table_->currentRow());
        });
        buttons_ = new QHBoxLayout;
        buttons_->addWidget(add);
        buttons_->addWidget(remove);
        buttons_->addStretch(1);
        auto* v = new QVBoxLayout(this);
        v->setContentsMargins(0, 0, 0, 0);
        v->addWidget(table_, 1);
        v->addLayout(buttons_);
    }

    QHBoxLayout* buttons() const { return buttons_; }

    void addRow(const EffectRow& r)
    {
        const int row = table_->rowCount();
        table_->insertRow(row);
        table_->setCellWidget(row, 0, idCombo(knownEffects(false), QStringLiteral("effect"), r.id));
        auto* lvl = new QSpinBox;
        lvl->setRange(1, 256);
        lvl->setValue(r.level);
        table_->setCellWidget(row, 1, lvl);
        auto* sec = new QDoubleSpinBox;
        sec->setRange(0.05, 100000000);
        sec->setDecimals(2);
        sec->setValue(r.seconds);
        table_->setCellWidget(row, 2, sec);
        if (chance_) {
            auto* chance = new QDoubleSpinBox;
            chance->setRange(0, 1);
            chance->setDecimals(2);
            chance->setSingleStep(0.1);
            chance->setValue(r.chance);
            chance->setToolTip(ItemEditorDialog::tr("1 - always, 0.5 - every other time"));
            table_->setCellWidget(row, 3, chance);
        }
    }

    void clear() { table_->setRowCount(0); }

    std::vector<EffectRow> rows() const
    {
        std::vector<EffectRow> out;
        for (int r = 0; r < table_->rowCount(); ++r) {
            EffectRow e;
            e.id = comboId(static_cast<QComboBox*>(table_->cellWidget(r, 0)));
            e.level = static_cast<QSpinBox*>(table_->cellWidget(r, 1))->value();
            e.seconds = static_cast<QDoubleSpinBox*>(table_->cellWidget(r, 2))->value();
            if (chance_)
                e.chance = static_cast<QDoubleSpinBox*>(table_->cellWidget(r, 3))->value();
            if (!e.id.isEmpty())
                out.push_back(e);
        }
        return out;
    }

    static std::unique_ptr<Tag> effectTag(const EffectRow& r)
    {
        auto e = std::make_unique<Tag>(TagType::Compound);
        setString(e.get(), "id", r.id);
        setInt(e.get(), TagType::Byte, "amplifier", r.level - 1);
        setInt(e.get(), TagType::Int, "duration", std::llround(r.seconds * 20));
        return e;
    }
    static EffectRow effectRow(const Tag* e, double chance)
    {
        return {stringOf(childOf(e, "id")), int(numberOf(childOf(e, "amplifier"))) + 1,
                numberOf(childOf(e, "duration"), 20) / 20.0, chance};
    }

    void readConsumeEffects(const Tag* list)
    {
        clear();
        if (!list || list->type != TagType::List)
            return;
        for (const auto& entry : list->children) {
            if (stringOf(childOf(entry.get(), "type")).remove(QStringLiteral("minecraft:")) != QLatin1String("apply_effects"))
                continue;
            const double chance = numberOf(childOf(entry.get(), "probability"), 1);
            if (const Tag* effects = childOf(entry.get(), "effects"); effects && effects->type == TagType::List)
                for (const auto& e : effects->children)
                    addRow(effectRow(e.get(), chance));
        }
    }
    static void writeConsumeEffects(Tag* list, const std::vector<EffectRow>& rows)
    {
        for (size_t i = list->children.size(); i-- > 0;)
            if (stringOf(childOf(list->children[i].get(), "type")).remove(QStringLiteral("minecraft:"))
                == QLatin1String("apply_effects"))
                list->take(i);
        for (const EffectRow& r : rows) {
            auto entry = std::make_unique<Tag>(TagType::Compound);
            setString(entry.get(), "type", QStringLiteral("minecraft:apply_effects"));
            ensureChild(entry.get(), TagType::List, "effects")->append(effectTag(r));
            setFloat(entry.get(), "probability", r.chance);
            list->append(std::move(entry));
        }
    }

private:
    bool chance_;
    QTableWidget* table_;
    QHBoxLayout* buttons_;
};

using AttributeRow = std::tuple<QString, double, QString, QString, QByteArray>;

struct ItemEditorDialog::Snapshot {
    QString id;
    int count = 1;
    QString name, itemName;
    QStringList lore;
    int durability = 0, maxDurability = 0;
    bool unbreakable = false;
    int repairCost = 0;
    std::vector<std::pair<QString, int>> enchantments;
    std::vector<QVariant> fields;
    std::vector<bool> groups;
    std::vector<EffectRow> consumeEffects, deathEffects, potionEffects;
    bool deathClears = false;
    int potionColor = -1, dyed = -1;
    std::vector<AttributeRow> attributes;
    int glint = -1;
    bool fireproof = false, hideTooltip = false;
};

ItemEditorDialog::ItemEditorDialog(const Tag& item, bool bedrock, int dataVersion, QWidget* parent)
    : QDialog(parent), item_(item.clone()), bedrock_(bedrock), dataVersion_(dataVersion)
{
    if (bedrock)
        format_ = Format::Bedrock;
    else if (item.child("components") || item.child("count") || dataVersion >= 3837)
        format_ = Format::JavaComponents;
    else
        format_ = Format::JavaLegacy;

    const auto stack = readItemStack(item);
    setWindowTitle(tr("Item: %1").arg(stack ? itemDisplayName(*stack) : QString()));
    if (stack)
        setWindowIcon(QIcon(itemPixmap(stack->id, 32, devicePixelRatioF())));
    resize(660, 640);

    tabs_ = new QTabWidget;
    buildMain();
    buildEnchantments();
    if (format_ == Format::JavaComponents) {
        buildFoodTab();
        buildPropertiesTab();
    }
    buildNbt();
    connect(tabs_, &QTabWidget::currentChanged, this, &ItemEditorDialog::tabChanged);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &ItemEditorDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(tabs_, 1);
    layout->addWidget(buttons);

    const std::pair<QWidget*, const char*> names[] = {
        {id_, "id"}, {count_, "count"}, {name_, "name"}, {itemName_, "item_name"}, {lore_, "lore"},
        {durability_, "durability"}, {maxDurability_, "maxDurability"}, {unbreakable_, "unbreakable"},
        {repairCost_, "repairCost"}, {glint_, "glint"}, {fireproof_, "fireproof"}, {hideTooltip_, "hideTooltip"},
        {deathClears_, "death_prevention.clear"}};
    for (const auto& [widget, name] : names)
        if (widget)
            widget->setObjectName(QLatin1String(name));

    loadForms();
}

ItemEditorDialog::~ItemEditorDialog()
{
    if (tree_)
        tree_->setModel(nullptr);
    if (treeUndo_)
        treeUndo_->clear();
}

int ItemEditorDialog::enchantmentLevelLimit() const
{
    return format_ == Format::JavaComponents ? 255 : 32767;
}

void ItemEditorDialog::addConsumeEffect(const QString& id, int level, double seconds, double chance)
{
    if (consumeEffects_)
        consumeEffects_->addRow({id, level, seconds, chance});
}

void ItemEditorDialog::addDeathEffect(const QString& id, int level, double seconds)
{
    if (deathEffects_)
        deathEffects_->addRow({id, level, seconds, 1});
}

bool ItemEditorDialog::nbtTab(int index) const
{
    return tabs_->widget(index) && tabs_->widget(index)->property("nbtTab").toBool();
}

void ItemEditorDialog::buildMain()
{
    auto* page = new QWidget;
    auto* pageLayout = new QVBoxLayout(page);
    auto* form = new QFormLayout;
    pageLayout->addLayout(form);
    pageLayout->addStretch(1);

    id_ = new QLineEdit;
    QStringList ids;
    for (const char* kind : {"item", "block"})
        for (const auto& [id, name] : GameAssets::instance().namedIds(QLatin1String(kind)))
            ids << id;
    ids.removeDuplicates();
    auto* completer = new QCompleter(ids, id_);
    completer->setFilterMode(Qt::MatchContains);
    id_->setCompleter(completer);
    form->addRow(tr("Item id:"), id_);

    count_ = new QSpinBox;
    count_->setRange(1, format_ == Format::JavaComponents ? 99 : 127);
    form->addRow(tr("Count:"), count_);

    name_ = new QLineEdit;
    name_->setPlaceholderText(tr("As the game names it"));
    name_->setToolTip(tr("Like a name given on an anvil: the game shows it in italics"));
    form->addRow(tr("Name:"), name_);
    if (format_ == Format::JavaComponents) {
        itemName_ = new QLineEdit;
        itemName_->setPlaceholderText(tr("As the game names it"));
        itemName_->setToolTip(tr("The item's own name (item_name): shown without italics, and an anvil does not "
                                 "change it"));
        form->addRow(tr("Own name:"), itemName_);
    }
    lore_ = new QPlainTextEdit;
    lore_->setPlaceholderText(tr("Lines under the name, one per line"));
    lore_->setMaximumHeight(90);
    form->addRow(tr("Lore:"), lore_);

    durability_ = new QSpinBox;
    durability_->setRange(0, 1000000);
    maxDurability_ = new QSpinBox;
    maxDurability_->setRange(0, 1000000);
    maxDurability_->setEnabled(format_ == Format::JavaComponents);
    maxDurability_->setToolTip(tr("0: the item does not wear out"));
    connect(maxDurability_, &QSpinBox::valueChanged, this, [this](int max) {
        durability_->setMaximum(std::max(max, 0));
        durability_->setEnabled(max > 0);
    });
    auto* dur = new QHBoxLayout;
    dur->addWidget(durability_, 1);
    dur->addWidget(new QLabel(tr("of")));
    dur->addWidget(maxDurability_, 1);
    form->addRow(tr("Durability:"), dur);
    unbreakable_ = new QCheckBox(tr("Unbreakable"));
    form->addRow(QString(), unbreakable_);

    repairCost_ = new QSpinBox;
    repairCost_->setRange(0, 1000000);
    repairCost_->setToolTip(tr("Extra levels an anvil asks for: it grows with every repair"));
    form->addRow(tr("Anvil cost:"), repairCost_);

    tabs_->addTab(page, tr("Main"));
}

void ItemEditorDialog::buildEnchantments()
{
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    enchantmentsNote_ = new QLabel;
    enchantmentsNote_->setWordWrap(true);
    v->addWidget(enchantmentsNote_);
    enchantments_ = new QTableWidget(0, 2);
    enchantments_->setHorizontalHeaderLabels({tr("Enchantment"), tr("Level")});
    enchantments_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    enchantments_->verticalHeader()->hide();
    v->addWidget(enchantments_, 1);
    auto* buttons = new QHBoxLayout;
    auto* add = new QPushButton(tr("Add"));
    auto* remove = new QPushButton(tr("Remove"));
    connect(add, &QPushButton::clicked, this, [this] { addEnchantment(QStringLiteral("minecraft:unbreaking"), 3); });
    connect(remove, &QPushButton::clicked, this, [this] {
        if (enchantments_->currentRow() >= 0)
            enchantments_->removeRow(enchantments_->currentRow());
    });
    buttons->addWidget(add);
    buttons->addWidget(remove);
    buttons->addStretch(1);
    v->addLayout(buttons);
    tabs_->addTab(page, tr("Enchantments"));
}

void ItemEditorDialog::addEnchantment(const QString& id, int level)
{
    QStringList ids = knownEnchantments();
    if (!bedrock_)
        for (const auto& [known, name] : GameAssets::instance().namedIds(QStringLiteral("enchantment")))
            if (!ids.contains(known))
                ids << known;
    const int row = enchantments_->rowCount();
    enchantments_->insertRow(row);
    enchantments_->setCellWidget(row, 0, idCombo(ids, QStringLiteral("enchantment"), id));
    auto* spin = new QSpinBox;
    spin->setRange(1, enchantmentLevelLimit());
    spin->setValue(level);
    enchantments_->setCellWidget(row, 1, spin);
    enchantments_->setCurrentCell(row, 0);
}

ItemEditorDialog::Group& ItemEditorDialog::addGroup(QWidget* page, const QString& component, const QString& title,
                                                   const QString& tip)
{
    Group g;
    g.component = component;
    g.box = new QGroupBox(QStringLiteral("%1 (%2)").arg(title, shortName(component)));
    g.box->setCheckable(true);
    g.box->setObjectName(shortName(component));
    if (!tip.isEmpty())
        g.box->setToolTip(tip);
    g.form = new QFormLayout(g.box);
    page->layout()->addWidget(g.box);
    groups_.push_back(g);
    return groups_.back();
}

QWidget* ItemEditorDialog::addField(const QString& component, const QString& key, Field::Kind kind,
                                    const QString& label, QFormLayout* form, QVariant absent, double min, double max)
{
    Field f;
    f.component = component;
    f.key = key;
    f.kind = kind;
    QWidget* w = nullptr;
    switch (kind) {
    case Field::Int: {
        auto* s = new QSpinBox;
        s->setRange(int(min), int(max));
        w = s;
        if (!absent.isValid())
            absent = int(min);
        break;
    }
    case Field::Float: {
        auto* s = new QDoubleSpinBox;
        s->setRange(min, max);
        s->setDecimals(2);
        w = s;
        if (!absent.isValid())
            absent = min;
        break;
    }
    case Field::Bool:
    case Field::Flag:
        w = new QCheckBox(label);
        if (!absent.isValid())
            absent = false;
        break;
    case Field::String:
        w = new QLineEdit;
        if (!absent.isValid())
            absent = QString();
        break;
    }
    f.absent = absent;
    f.widget = w;
    w->setObjectName(key.isEmpty() ? shortName(component) : shortName(component) + QLatin1Char('.') + key);
    if (kind == Field::Bool || kind == Field::Flag)
        form->addRow(QString(), w);
    else
        form->addRow(label, w);
    fields_.push_back(f);
    return w;
}

void ItemEditorDialog::choice(QWidget* field, const QStringList& values)
{
    auto* edit = qobject_cast<QLineEdit*>(field);
    auto* completer = new QCompleter(values, edit);
    completer->setFilterMode(Qt::MatchContains);
    completer->setCompletionMode(QCompleter::UnfilteredPopupCompletion);
    edit->setCompleter(completer);
    edit->setPlaceholderText(values.value(0));
}

void ItemEditorDialog::buildFoodTab()
{
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* page = new QWidget;
    new QVBoxLayout(page);

    Group* g = &addGroup(page, QStringLiteral("minecraft:food"), tr("Can be eaten"));
    addField(g->component, QStringLiteral("nutrition"), Field::Int, tr("Food:"), g->form, 0, 0, 1000)
        ->setToolTip(tr("Half drumsticks of the food bar it fills"));
    addField(g->component, QStringLiteral("saturation"), Field::Float, tr("Saturation:"), g->form, 0.0, 0, 1000);
    addField(g->component, QStringLiteral("can_always_eat"), Field::Bool, tr("Even with a full food bar"), g->form);

    g = &addGroup(page, QStringLiteral("minecraft:consumable"), tr("Is used up when eaten or drunk"));
    addField(g->component, QStringLiteral("consume_seconds"), Field::Float, tr("Takes, seconds:"), g->form, 1.6, 0, 3600);
    choice(addField(g->component, QStringLiteral("animation"), Field::String, tr("Animation:"), g->form, QStringLiteral("eat")),
           {QStringLiteral("eat"), QStringLiteral("drink"), QStringLiteral("none"), QStringLiteral("block"),
            QStringLiteral("bow"), QStringLiteral("spear"), QStringLiteral("crossbow"), QStringLiteral("spyglass"),
            QStringLiteral("toot_horn"), QStringLiteral("brush"), QStringLiteral("bundle")});
    addField(g->component, QStringLiteral("has_consume_particles"), Field::Bool, tr("Crumbs while eating"), g->form, true);
    choice(addField(g->component, QStringLiteral("sound"), Field::String, tr("Sound:"), g->form),
           {QStringLiteral("minecraft:entity.generic.eat"), QStringLiteral("minecraft:entity.generic.drink"),
            QStringLiteral("minecraft:item.honey_bottle.drink")});
    consumeEffects_ = new EffectsTable(true);
    g->form->addRow(new QLabel(tr("Effects when used up:")));
    g->form->addRow(consumeEffects_);

    g = &addGroup(page, QStringLiteral("minecraft:death_prevention"), tr("Saves from death like a totem"),
                  tr("Held in a hand when the holder would die: it is used up instead (Java 1.21.2+)"));
    deathClears_ = new QCheckBox(tr("Takes all effects away first"));
    g->form->addRow(QString(), deathClears_);
    deathEffects_ = new EffectsTable(true);
    auto* totem = new QPushButton(tr("As a totem"));
    totem->setToolTip(tr("Regeneration II 45 s, absorption II 5 s, fire resistance 40 s"));
    connect(totem, &QPushButton::clicked, this, [this] {
        deathClears_->setChecked(true);
        deathEffects_->clear();
        deathEffects_->addRow({QStringLiteral("minecraft:regeneration"), 2, 45, 1});
        deathEffects_->addRow({QStringLiteral("minecraft:absorption"), 2, 5, 1});
        deathEffects_->addRow({QStringLiteral("minecraft:fire_resistance"), 1, 40, 1});
    });
    deathEffects_->buttons()->insertWidget(2, totem);
    g->form->addRow(new QLabel(tr("Effects then:")));
    g->form->addRow(deathEffects_);

    g = &addGroup(page, QStringLiteral("minecraft:potion_contents"), tr("Potion"),
                  tr("Potions, splash and lingering potions, tipped arrows"));
    choice(addField(g->component, QStringLiteral("potion"), Field::String, tr("Potion:"), g->form), kPotions);
    potionColorOn_ = new QCheckBox(tr("Own colour:"));
    potionColor_ = colorButton(this);
    auto* colorRow = new QHBoxLayout;
    colorRow->addWidget(potionColorOn_);
    colorRow->addWidget(potionColor_);
    colorRow->addStretch(1);
    g->form->addRow(colorRow);
    potionEffects_ = new EffectsTable(false);
    g->form->addRow(new QLabel(tr("Extra effects:")));
    g->form->addRow(potionEffects_);

    g = &addGroup(page, QStringLiteral("minecraft:use_remainder"), tr("Leaves something after use"),
                  tr("Like the bottle left of a potion"));
    QStringList itemIds;
    for (const auto& [id, name] : GameAssets::instance().namedIds(QStringLiteral("item")))
        itemIds << id;
    choice(addField(g->component, QStringLiteral("id"), Field::String, tr("Item:"), g->form,
                    QStringLiteral("minecraft:glass_bottle")),
           itemIds);
    addField(g->component, QStringLiteral("count"), Field::Int, tr("Count:"), g->form, 1, 1, 99);

    g = &addGroup(page, QStringLiteral("minecraft:use_cooldown"), tr("Cooldown after use"));
    addField(g->component, QStringLiteral("seconds"), Field::Float, tr("Seconds:"), g->form, 1.0, 0, 3600);
    addField(g->component, QStringLiteral("cooldown_group"), Field::String, tr("Shared with:"), g->form)
        ->setToolTip(tr("Items with the same group cool down together, like ender pearls"));

    static_cast<QVBoxLayout*>(page->layout())->addStretch(1);
    scroll->setWidget(page);
    tabs_->addTab(scroll, tr("Food and potions"));
}

void ItemEditorDialog::buildPropertiesTab()
{
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);

    auto* attrBox = new QGroupBox(tr("Attributes when held or worn (attribute_modifiers)"));
    auto* attrLayout = new QVBoxLayout(attrBox);
    attributes_ = new QTableWidget(0, 4);
    attributes_->setHorizontalHeaderLabels({tr("Attribute"), tr("Amount"), tr("How"), tr("Slot")});
    attributes_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    attributes_->verticalHeader()->hide();
    attributes_->setMinimumHeight(110);
    attrLayout->addWidget(attributes_);
    auto* attrButtons = new QHBoxLayout;
    auto* addAttr = new QPushButton(tr("Add"));
    auto* removeAttr = new QPushButton(tr("Remove"));
    connect(addAttr, &QPushButton::clicked, this,
            [this] { addAttribute(QStringLiteral("attack_damage"), 5, QStringLiteral("add_value"), QStringLiteral("mainhand")); });
    connect(removeAttr, &QPushButton::clicked, this, [this] {
        if (attributes_->currentRow() >= 0)
            attributes_->removeRow(attributes_->currentRow());
    });
    attrButtons->addWidget(addAttr);
    attrButtons->addWidget(removeAttr);
    attrButtons->addStretch(1);
    attrLayout->addLayout(attrButtons);
    v->addWidget(attrBox);

    const QStringList slots_{QStringLiteral("head"), QStringLiteral("chest"), QStringLiteral("legs"),
                             QStringLiteral("feet"), QStringLiteral("body"), QStringLiteral("mainhand"),
                             QStringLiteral("offhand"), QStringLiteral("saddle")};
    Group* g = &addGroup(page, QStringLiteral("minecraft:equippable"), tr("Can be worn"));
    choice(addField(g->component, QStringLiteral("slot"), Field::String, tr("Slot:"), g->form, QStringLiteral("head")), slots_);
    addField(g->component, QStringLiteral("asset_id"), Field::String, tr("Look (asset_id):"), g->form)
        ->setToolTip(tr("The armour look worn on the body, like minecraft:diamond"));
    choice(addField(g->component, QStringLiteral("equip_sound"), Field::String, tr("Sound:"), g->form),
           {QStringLiteral("minecraft:item.armor.equip_diamond"), QStringLiteral("minecraft:item.armor.equip_netherite")});
    addField(g->component, QStringLiteral("camera_overlay"), Field::String, tr("Screen overlay:"), g->form)
        ->setToolTip(tr("A picture over the screen, like minecraft:misc/pumpkinblur"));
    addField(g->component, QStringLiteral("swappable"), Field::Bool, tr("Put on with a right click"), g->form, true);
    addField(g->component, QStringLiteral("dispensable"), Field::Bool, tr("A dispenser can put it on"), g->form, true);
    addField(g->component, QStringLiteral("damage_on_hurt"), Field::Bool, tr("Wears out when the wearer is hurt"), g->form, true);

    g = &addGroup(page, QStringLiteral("minecraft:tool"), tr("A tool"));
    addField(g->component, QStringLiteral("default_mining_speed"), Field::Float, tr("Mining speed:"), g->form, 1.0, 0, 1000);
    addField(g->component, QStringLiteral("damage_per_block"), Field::Int, tr("Wear per block:"), g->form, 1, 0, 1000);

    g = &addGroup(page, QStringLiteral("minecraft:weapon"), tr("A weapon"), tr("Java 1.21.5+"));
    addField(g->component, QStringLiteral("item_damage_per_attack"), Field::Int, tr("Wear per hit:"), g->form, 1, 0, 1000);
    addField(g->component, QStringLiteral("disable_blocking_for_seconds"), Field::Float, tr("Breaks a shield for, s:"),
             g->form, 0.0, 0, 3600);

    g = &addGroup(page, QStringLiteral("minecraft:enchantable"), tr("Can be enchanted on a table"));
    addField(g->component, QStringLiteral("value"), Field::Int, tr("Enchantability:"), g->form, 10, 1, 1000)
        ->setToolTip(tr("Higher: better enchantments. Gold 22, diamond 10, netherite 15"));

    g = &addGroup(page, QStringLiteral("minecraft:repairable"), tr("Repaired on an anvil with"));
    choice(addField(g->component, QStringLiteral("items"), Field::String, tr("Item or tag:"), g->form,
                    QStringLiteral("minecraft:diamond")),
           {QStringLiteral("minecraft:diamond"), QStringLiteral("minecraft:iron_ingot"), QStringLiteral("minecraft:netherite_ingot"),
            QStringLiteral("#minecraft:repairs_iron_armor"), QStringLiteral("minecraft:phantom_membrane")});

    auto* other = new QGroupBox(tr("Other"));
    auto* form = new QFormLayout(other);
    v->addWidget(other);
    addField(QStringLiteral("minecraft:max_stack_size"), {}, Field::Int, tr("Stack size:"), form, 0, 0, 99)
        ->setToolTip(tr("0: as usual"));
    choice(addField(QStringLiteral("minecraft:rarity"), {}, Field::String, tr("Rarity (name colour):"), form),
           {QStringLiteral("common"), QStringLiteral("uncommon"), QStringLiteral("rare"), QStringLiteral("epic")});
    glint_ = new QComboBox;
    glint_->addItem(tr("as usual"), -1);
    glint_->addItem(tr("always"), 1);
    glint_->addItem(tr("never"), 0);
    form->addRow(tr("Shimmer:"), glint_);
    addField(QStringLiteral("minecraft:item_model"), {}, Field::String, tr("Model (item_model):"), form)
        ->setToolTip(tr("Looks like another item, e.g. minecraft:diamond (Java 1.21.4+)"));
    dyedOn_ = new QCheckBox(tr("Dyed:"));
    dyed_ = colorButton(this);
    auto* dyedRow = new QHBoxLayout;
    dyedRow->addWidget(dyedOn_);
    dyedRow->addWidget(dyed_);
    dyedRow->addStretch(1);
    form->addRow(dyedRow);
    addField(QStringLiteral("minecraft:glider"), {}, Field::Flag, tr("Glides like elytra (worn on the chest)"), form);
    fireproof_ = new QCheckBox(tr("Does not burn in fire and lava"));
    form->addRow(QString(), fireproof_);
    hideTooltip_ = new QCheckBox(tr("No tooltip at all"));
    form->addRow(QString(), hideTooltip_);
    addField(QStringLiteral("minecraft:intangible_projectile"), {}, Field::Flag,
             tr("Shot as a ghost: cannot be picked up"), form);

    v->addStretch(1);
    scroll->setWidget(page);
    tabs_->addTab(scroll, tr("Properties"));
}

void ItemEditorDialog::addAttribute(const QString& attribute, double amount, const QString& operation, const QString& slot)
{
    QString shortAttr = attribute.mid(attribute.indexOf(QLatin1Char(':')) + 1);
    for (const char* prefix : {"generic.", "player."})
        if (shortAttr.startsWith(QLatin1String(prefix)))
            shortAttr = shortAttr.mid(int(std::strlen(prefix)));
    const int row = attributes_->rowCount();
    attributes_->insertRow(row);
    auto* type = new QComboBox;
    type->setEditable(true);
    for (const QString& a : kAttributes) {
        const QString text = GameAssets::instance().translate(QStringLiteral("attribute.name.") + a);
        type->addItem(text.isEmpty() ? prettifyId(a) : text, a);
    }
    int at = type->findData(shortAttr);
    if (at < 0) {
        type->addItem(shortAttr, shortAttr);
        at = type->count() - 1;
    }
    type->setCurrentIndex(at);
    attributes_->setCellWidget(row, 0, type);
    auto* value = new QDoubleSpinBox;
    value->setRange(-1000000, 1000000);
    value->setDecimals(3);
    value->setValue(amount);
    attributes_->setCellWidget(row, 1, value);
    auto* op = new QComboBox;
    op->addItem(tr("add"), QStringLiteral("add_value"));
    op->addItem(tr("× base"), QStringLiteral("add_multiplied_base"));
    op->addItem(tr("× total"), QStringLiteral("add_multiplied_total"));
    op->setCurrentIndex(std::max(0, op->findData(operation)));
    attributes_->setCellWidget(row, 2, op);
    auto* where = new QComboBox;
    for (const char* s : {"any", "mainhand", "offhand", "hand", "head", "chest", "legs", "feet", "armor", "body", "saddle"})
        where->addItem(QLatin1String(s), QLatin1String(s));
    where->setCurrentIndex(std::max(0, where->findData(slot.isEmpty() ? QStringLiteral("any") : slot)));
    attributes_->setCellWidget(row, 3, where);
}

void ItemEditorDialog::buildNbt()
{
    auto* page = new QWidget;
    page->setProperty("nbtTab", true);
    auto* v = new QVBoxLayout(page);
    auto* note = new QLabel(tr("All tags of the item. Double click a value to change it; any tag can be added, "
                               "for example inside a component."));
    note->setWordWrap(true);
    v->addWidget(note);
    tree_ = new QTreeView;
    tree_->setUniformRowHeights(true);
    tree_->setAlternatingRowColors(true);
    tree_->header()->setStretchLastSection(true);
    tree_->setItemDelegate(new NbtItemDelegate(tree_));
    v->addWidget(tree_, 1);
    treeUndo_ = new QUndoStack(this);

    auto* buttons = new QHBoxLayout;
    auto* add = new QPushButton(tr("Add a tag…"));
    auto* remove = new QPushButton(tr("Delete the tag"));
    connect(add, &QPushButton::clicked, this, [this] {
        Tag* t = treeModel_ ? treeModel_->tagFromIndex(tree_->currentIndex().siblingAtColumn(0)) : nullptr;
        if (!t)
            t = item_.get();
        Tag* container = nbt::isContainer(t->type) ? t : t->parent;
        if (!container)
            return;
        AddTagDialog dialog(container, this);
        if (dialog.exec() != QDialog::Accepted)
            return;
        const QModelIndex parent = treeModel_->indexFromTag(container);
        const QModelIndex index = treeModel_->insertTag(parent, int(container->children.size()),
                                                        std::make_unique<Tag>(dialog.type(), dialog.name().toStdString()));
        if (index.isValid()) {
            tree_->expand(parent);
            tree_->setCurrentIndex(index);
            const TagType type = dialog.type();
            if (nbt::isInteger(type) || nbt::isFloating(type) || type == TagType::String)
                tree_->edit(index.siblingAtColumn(NbtModel::ValueColumn));
        }
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        if (!treeModel_)
            return;
        const QModelIndex index = tree_->currentIndex().siblingAtColumn(0);
        Tag* t = treeModel_->tagFromIndex(index);
        if (t && t != item_.get() && !treeModel_->isDeleted(t))
            treeModel_->removeTag(index);
    });
    buttons->addWidget(add);
    buttons->addWidget(remove);
    buttons->addStretch(1);
    v->addLayout(buttons);
    tabs_->addTab(page, tr("NBT"));
}

void ItemEditorDialog::rebuildNbt()
{
    tree_->setModel(nullptr);
    treeUndo_->clear();
    treeModel_ = std::make_unique<NbtModel>(item_.get(), tr("item"), treeUndo_);
    tree_->setModel(treeModel_.get());
    tree_->expandAll();
    tree_->resizeColumnToContents(NbtModel::NameColumn);
}

void ItemEditorDialog::tabChanged(int index)
{
    const bool wasNbt = nbtTab(previousTab_), isNbt = nbtTab(index);
    if (!wasNbt && isNbt) {
        applyForms();
        rebuildNbt();
    } else if (wasNbt && !isNbt) {
        loadForms();
    }
    previousTab_ = index;
}

QVariant ItemEditorDialog::fieldValue(const Field& f) const
{
    switch (f.kind) {
    case Field::Int: return static_cast<QSpinBox*>(f.widget)->value();
    case Field::Float: return static_cast<QDoubleSpinBox*>(f.widget)->value();
    case Field::Bool:
    case Field::Flag: return static_cast<QCheckBox*>(f.widget)->isChecked();
    case Field::String: return static_cast<QLineEdit*>(f.widget)->text().trimmed();
    }
    return {};
}

void ItemEditorDialog::setFieldValue(const Field& f, const QVariant& v)
{
    switch (f.kind) {
    case Field::Int: static_cast<QSpinBox*>(f.widget)->setValue(v.toInt()); break;
    case Field::Float: static_cast<QDoubleSpinBox*>(f.widget)->setValue(v.toDouble()); break;
    case Field::Bool:
    case Field::Flag: static_cast<QCheckBox*>(f.widget)->setChecked(v.toBool()); break;
    case Field::String: static_cast<QLineEdit*>(f.widget)->setText(v.toString()); break;
    }
}

void ItemEditorDialog::loadForms()
{
    const Tag* item = item_.get();
    const auto stack = readItemStack(*item);
    const Tag* components = childOf(item, "components");
    const Tag* tag = childOf(item, "tag");
    const Tag* display = childOf(tag, "display");

    id_->setText(stack ? stack->id : stringOf(childOf(item, bedrock_ ? "Name" : "id")));
    count_->setValue(stack ? stack->count : 1);
    const Tag* customName = format_ == Format::JavaComponents ? childOf(components, "minecraft:custom_name")
                                                              : childOf(display, "Name");
    name_->setText(customName ? textComponentText(*customName) : QString());
    if (itemName_) {
        const Tag* own = childOf(components, "minecraft:item_name");
        itemName_->setText(own ? textComponentText(*own) : QString());
    }

    QStringList lore;
    const Tag* loreList = format_ == Format::JavaComponents ? childOf(components, "minecraft:lore") : childOf(display, "Lore");
    if (loreList && loreList->type == TagType::List)
        for (const auto& line : loreList->children)
            lore << textComponentText(*line);
    lore_->setPlainText(lore.join(QLatin1Char('\n')));

    const int max = stack ? stack->maxDamage : 0;
    maxDurability_->setValue(max);
    durability_->setMaximum(std::max(max, 0));
    durability_->setValue(stack && max > 0 ? max - stack->damage : 0);
    durability_->setEnabled(max > 0);
    unbreakable_->setChecked(format_ == Format::JavaComponents ? childOf(components, "minecraft:unbreakable") != nullptr
                                                               : numberOf(childOf(tag, "Unbreakable")) != 0);
    repairCost_->setValue(int(numberOf(format_ == Format::JavaComponents ? childOf(components, "minecraft:repair_cost")
                                                                         : childOf(tag, "RepairCost"))));

    enchantments_->setRowCount(0);
    const bool book = id_->text().endsWith(QLatin1String("enchanted_book"));
    enchantmentsNote_->setText(book ? tr("The enchantments stored in this book: an anvil puts them on an item.")
                                    : tr("Any enchantment at any level up to %1, also ones the game does not allow "
                                         "on this item.")
                                          .arg(enchantmentLevelLimit()));
    if (stack)
        for (const auto& [id, level] : stack->enchantments)
            addEnchantment(id, level);

    if (format_ == Format::JavaComponents) {
        for (Group& g : groups_)
            g.box->setChecked(childOf(components, g.component) != nullptr);
        for (const Field& f : fields_) {
            const Tag* c = childOf(components, f.component);
            if (f.key.isEmpty()) {
                if (f.kind == Field::Flag)
                    setFieldValue(f, c != nullptr);
                else if (f.kind == Field::String)
                    setFieldValue(f, c ? stringOf(c) : f.absent);
                else
                    setFieldValue(f, c ? QVariant(numberOf(c)) : f.absent);
                continue;
            }
            const Tag* v = c && c->type == TagType::String && f.key == QLatin1String("potion") ? c : childOf(c, f.key);
            if (!v)
                setFieldValue(f, f.kind == Field::String ? QVariant(QString()) : f.absent);
            else if (f.kind == Field::String)
                setFieldValue(f, stringOf(v));
            else if (f.kind == Field::Bool)
                setFieldValue(f, numberOf(v) != 0);
            else
                setFieldValue(f, numberOf(v));
        }
        const Tag* consumable = childOf(components, "minecraft:consumable");
        consumeEffects_->readConsumeEffects(childOf(consumable, "on_consume_effects"));
        const Tag* death = childOf(components, "minecraft:death_prevention");
        deathEffects_->readConsumeEffects(childOf(death, "death_effects"));
        bool clears = false;
        if (const Tag* list = childOf(death, "death_effects"); list && list->type == TagType::List)
            for (const auto& e : list->children)
                clears = clears || stringOf(childOf(e.get(), "type")).endsWith(QLatin1String("clear_all_effects"));
        deathClears_->setChecked(clears);
        const Tag* potion = childOf(components, "minecraft:potion_contents");
        potionEffects_->clear();
        if (const Tag* list = childOf(potion, "custom_effects"); list && list->type == TagType::List)
            for (const auto& e : list->children)
                potionEffects_->addRow(EffectsTable::effectRow(e.get(), 1));
        const Tag* color = childOf(potion, "custom_color");
        potionColorOn_->setChecked(color != nullptr);
        if (color)
            showColor(potionColor_, QColor::fromRgb(QRgb(numberOf(color)) & 0xffffff));

        attributes_->setRowCount(0);
        const Tag* modifiers = childOf(components, "minecraft:attribute_modifiers");
        if (modifiers && modifiers->type == TagType::Compound)
            modifiers = childOf(modifiers, "modifiers");
        if (modifiers && modifiers->type == TagType::List)
            for (const auto& m : modifiers->children) {
                addAttribute(stringOf(childOf(m.get(), "type")), numberOf(childOf(m.get(), "amount")),
                             stringOf(childOf(m.get(), "operation")), stringOf(childOf(m.get(), "slot")));
                auto copy = m->clone();
                attributes_->cellWidget(attributes_->rowCount() - 1, 0)
                    ->setProperty("original", NbtModel::serializeTags({copy.get()}));
            }

        const Tag* glint = childOf(components, "minecraft:enchantment_glint_override");
        glint_->setCurrentIndex(glint_->findData(glint ? (numberOf(glint) != 0 ? 1 : 0) : -1));
        fireproof_->setChecked(childOf(components, "minecraft:damage_resistant") != nullptr
                               || childOf(components, "minecraft:fire_resistant") != nullptr);
        hideTooltip_->setChecked(childOf(components, "minecraft:hide_tooltip") != nullptr
                                 || numberOf(childOf(childOf(components, "minecraft:tooltip_display"), "hide_tooltip")) != 0);
        const Tag* dyed = childOf(components, "minecraft:dyed_color");
        if (dyed && dyed->type == TagType::Compound)
            dyed = childOf(dyed, "rgb");
        dyedOn_->setChecked(dyed != nullptr);
        if (dyed)
            showColor(dyed_, QColor::fromRgb(QRgb(numberOf(dyed)) & 0xffffff));
    }
    loaded_ = std::make_unique<Snapshot>(current());
}

ItemEditorDialog::Snapshot ItemEditorDialog::current() const
{
    Snapshot s;
    s.id = id_->text().trimmed();
    if (!s.id.isEmpty() && !s.id.contains(QLatin1Char(':')))
        s.id.prepend(QStringLiteral("minecraft:"));
    s.count = count_->value();
    s.name = name_->text();
    if (itemName_)
        s.itemName = itemName_->text();
    s.lore = lore_->toPlainText().isEmpty() ? QStringList() : lore_->toPlainText().split(QLatin1Char('\n'));
    s.maxDurability = maxDurability_->value();
    s.durability = durability_->value();
    s.unbreakable = unbreakable_->isChecked();
    s.repairCost = repairCost_->value();
    for (int r = 0; r < enchantments_->rowCount(); ++r) {
        const auto* combo = qobject_cast<QComboBox*>(enchantments_->cellWidget(r, 0));
        const auto* spin = qobject_cast<QSpinBox*>(enchantments_->cellWidget(r, 1));
        if (combo && spin && !comboId(combo).isEmpty())
            s.enchantments.emplace_back(comboId(combo), spin->value());
    }
    if (format_ == Format::JavaComponents) {
        for (const Field& f : fields_)
            s.fields.push_back(fieldValue(f));
        for (const Group& g : groups_)
            s.groups.push_back(g.box->isChecked());
        s.consumeEffects = consumeEffects_->rows();
        s.deathEffects = deathEffects_->rows();
        s.deathClears = deathClears_->isChecked();
        s.potionEffects = potionEffects_->rows();
        s.potionColor = potionColorOn_->isChecked() ? int(potionColor_->property("color").value<QColor>().rgb() & 0xffffff) : -1;
        for (int r = 0; r < attributes_->rowCount(); ++r) {
            auto* type = static_cast<QComboBox*>(attributes_->cellWidget(r, 0));
            const int at = type->findText(type->currentText());
            const QString attr = at >= 0 ? type->itemData(at).toString() : type->currentText().trimmed();
            s.attributes.emplace_back(attr, static_cast<QDoubleSpinBox*>(attributes_->cellWidget(r, 1))->value(),
                                      static_cast<QComboBox*>(attributes_->cellWidget(r, 2))->currentData().toString(),
                                      static_cast<QComboBox*>(attributes_->cellWidget(r, 3))->currentData().toString(),
                                      type->property("original").toByteArray());
        }
        s.glint = glint_->currentData().toInt();
        s.fireproof = fireproof_->isChecked();
        s.hideTooltip = hideTooltip_->isChecked();
        s.dyed = dyedOn_->isChecked() ? int(dyed_->property("color").value<QColor>().rgb() & 0xffffff) : -1;
    }
    return s;
}

void ItemEditorDialog::applyForms()
{
    if (!loaded_)
        return;
    const Snapshot now = current();
    const Snapshot& was = *loaded_;
    Tag* item = item_.get();
    const bool modern = format_ == Format::JavaComponents;
    auto components = [&] { return ensureChild(item, TagType::Compound, "components"); };
    auto legacyTag = [&] { return ensureChild(item, TagType::Compound, "tag"); };
    const bool before1215 = dataVersion_ > 0 && dataVersion_ < 4325;

    if (now.id != was.id && !now.id.isEmpty())
        setString(item, bedrock_ ? "Name" : "id", now.id);
    if (now.count != was.count) {
        if (modern)
            setInt(item, TagType::Int, "count", now.count);
        else
            setInt(item, TagType::Byte, "Count", now.count);
    }

    auto jsonTextFor = [&](const Tag* old) {
        if (bedrock_)
            return false;
        if (format_ == Format::JavaLegacy)
            return true;
        if (old && old->type == TagType::String)
            return old->string.starts_with('{') || old->string.starts_with('"');
        if (old)
            return false;
        return before1215;
    };
    auto text = [&](const QString& plain, bool json) {
        return json ? QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("text"), plain},
                                                                  {QStringLiteral("italic"), false}})
                                            .toJson(QJsonDocument::Compact))
                    : plain;
    };
    if (now.name != was.name) {
        if (modern) {
            const bool json = jsonTextFor(childOf(childOf(item, "components"), "minecraft:custom_name"));
            if (now.name.isEmpty())
                removeChild(childOf(item, "components"), "minecraft:custom_name");
            else
                setString(components(), "minecraft:custom_name", text(now.name, json));
        } else if (now.name.isEmpty()) {
            removeChild(childOf(childOf(item, "tag"), "display"), "Name");
        } else {
            setString(ensureChild(legacyTag(), TagType::Compound, "display"), "Name", text(now.name, jsonTextFor(nullptr)));
        }
    }
    if (modern && now.itemName != was.itemName) {
        const bool json = jsonTextFor(childOf(childOf(item, "components"), "minecraft:item_name"));
        if (now.itemName.isEmpty())
            removeChild(childOf(item, "components"), "minecraft:item_name");
        else
            setString(components(), "minecraft:item_name", text(now.itemName, json));
    }
    if (now.lore != was.lore) {
        Tag* parent = modern ? components() : ensureChild(legacyTag(), TagType::Compound, "display");
        const char* key = modern ? "minecraft:lore" : "Lore";
        const Tag* old = childOf(parent, key);
        const bool json = jsonTextFor(old && !old->children.empty() ? old->children.front().get() : nullptr);
        removeChild(parent, key);
        if (!now.lore.isEmpty()) {
            Tag* list = ensureChild(parent, TagType::List, key);
            list->listType = TagType::String;
            for (const QString& line : now.lore)
                list->append(std::make_unique<Tag>(TagType::String))->string = text(line, json).toStdString();
        }
    }

    if (now.maxDurability != was.maxDurability && modern) {
        if (now.maxDurability == justnbt::defaultMaxDamage(now.id))
            removeChild(childOf(item, "components"), "minecraft:max_damage");
        else
            setInt(components(), TagType::Int, "minecraft:max_damage", now.maxDurability);
    }
    if (now.durability != was.durability || now.maxDurability != was.maxDurability) {
        const int damage = std::max(0, now.maxDurability - now.durability);
        if (modern) {
            if (damage == 0)
                removeChild(childOf(item, "components"), "minecraft:damage");
            else
                setInt(components(), TagType::Int, "minecraft:damage", damage);
        } else if (damage == 0) {
            removeChild(childOf(item, "tag"), "Damage");
        } else {
            setInt(legacyTag(), TagType::Int, "Damage", damage);
        }
    }
    if (now.unbreakable != was.unbreakable) {
        if (modern) {
            if (now.unbreakable)
                ensureChild(components(), TagType::Compound, "minecraft:unbreakable");
            else
                removeChild(childOf(item, "components"), "minecraft:unbreakable");
        } else if (now.unbreakable) {
            setInt(legacyTag(), TagType::Byte, "Unbreakable", 1);
        } else {
            removeChild(childOf(item, "tag"), "Unbreakable");
        }
    }
    if (now.repairCost != was.repairCost) {
        Tag* parent = modern ? components() : legacyTag();
        const char* key = modern ? "minecraft:repair_cost" : "RepairCost";
        if (now.repairCost == 0)
            removeChild(parent, key);
        else
            setInt(parent, TagType::Int, key, now.repairCost);
    }

    if (now.enchantments != was.enchantments) {
        const bool book = now.id.endsWith(QLatin1String("enchanted_book"));
        if (modern) {
            const char* key = book ? "minecraft:stored_enchantments" : "minecraft:enchantments";
            Tag* comp = components();
            Tag* old = childOf(comp, key);
            const bool wrapped = old && old->type == TagType::Compound && childOf(old, "levels");
            if (now.enchantments.empty()) {
                removeChild(comp, key);
            } else {
                Tag* target = ensureChild(comp, TagType::Compound, key);
                if (wrapped)
                    target = ensureChild(target, TagType::Compound, "levels");
                for (size_t i = target->children.size(); i-- > 0;)
                    if (nbt::isInteger(target->children[i]->type))
                        target->take(i);
                for (const auto& [id, level] : now.enchantments)
                    setInt(target, TagType::Int, id.toStdString(), level);
            }
        } else {
            Tag* t = legacyTag();
            const char* key = bedrock_ ? "ench" : book ? "StoredEnchantments" : "Enchantments";
            removeChild(t, key);
            if (!now.enchantments.empty()) {
                Tag* list = ensureChild(t, TagType::List, key);
                for (const auto& [id, level] : now.enchantments) {
                    auto e = std::make_unique<Tag>(TagType::Compound);
                    if (bedrock_) {
                        const int number = bedrockEnchantmentNumber(id);
                        if (number < 0)
                            continue;
                        setInt(e.get(), TagType::Short, "id", number);
                    } else {
                        setString(e.get(), "id", id);
                    }
                    setInt(e.get(), TagType::Short, "lvl", level);
                    list->append(std::move(e));
                }
            }
        }
    }

    if (modern) {
        Tag* comp = components();
        auto groupOn = [&](const Snapshot& s, const QString& component) {
            for (size_t i = 0; i < groups_.size(); ++i)
                if (groups_[i].component == component)
                    return bool(s.groups[i]);
            return true;
        };
        for (size_t i = 0; i < groups_.size(); ++i)
            if (!now.groups[i] && was.groups[i])
                removeChild(comp, groups_[i].component.toStdString());
        for (size_t i = 0; i < fields_.size(); ++i) {
            const Field& f = fields_[i];
            const std::string component = f.component.toStdString();
            const QVariant& v = now.fields[i];
            if (f.key.isEmpty()) {
                if (v == was.fields[i])
                    continue;
                if (v == f.absent || (f.kind == Field::String && v.toString().isEmpty()))
                    removeChild(comp, component);
                else if (f.kind == Field::Flag)
                    ensureChild(comp, TagType::Compound, component);
                else if (f.kind == Field::Int)
                    setInt(comp, TagType::Int, component, v.toInt());
                else if (f.kind == Field::Float)
                    setFloat(comp, component, v.toDouble());
                else
                    setString(comp, component, v.toString());
                continue;
            }
            const bool on = groupOn(now, f.component), wasOn = groupOn(was, f.component);
            if (!on || (wasOn && v == was.fields[i]))
                continue;
            Tag* c = ensureChild(comp, TagType::Compound, component);
            const std::string key = f.key.toStdString();
            if (f.kind == Field::String) {
                if (v.toString().isEmpty())
                    removeChild(c, key);
                else
                    setString(c, key, v.toString());
            } else if (f.kind == Field::Bool) {
                if (v == f.absent)
                    removeChild(c, key);
                else
                    setInt(c, TagType::Byte, key, v.toBool());
            } else if (f.kind == Field::Int) {
                setInt(c, TagType::Int, key, v.toInt());
            } else {
                setFloat(c, key, v.toDouble());
            }
        }
        for (size_t i = 0; i < groups_.size(); ++i)
            if (now.groups[i] && !was.groups[i])
                ensureChild(comp, TagType::Compound, groups_[i].component.toStdString());

        if (now.groups != was.groups || now.consumeEffects != was.consumeEffects)
            if (Tag* c = groupOn(now, QStringLiteral("minecraft:consumable")) ? childOf(comp, "minecraft:consumable") : nullptr) {
                EffectsTable::writeConsumeEffects(ensureChild(c, TagType::List, "on_consume_effects"), now.consumeEffects);
                if (childOf(c, "on_consume_effects")->children.empty())
                    removeChild(c, "on_consume_effects");
            }
        if (now.groups != was.groups || now.deathEffects != was.deathEffects || now.deathClears != was.deathClears)
            if (Tag* d = groupOn(now, QStringLiteral("minecraft:death_prevention")) ? childOf(comp, "minecraft:death_prevention")
                                                                                   : nullptr) {
                Tag* list = ensureChild(d, TagType::List, "death_effects");
                for (size_t i = list->children.size(); i-- > 0;)
                    if (stringOf(childOf(list->children[i].get(), "type")).endsWith(QLatin1String("clear_all_effects")))
                        list->take(i);
                if (now.deathClears) {
                    auto clear = std::make_unique<Tag>(TagType::Compound);
                    setString(clear.get(), "type", QStringLiteral("minecraft:clear_all_effects"));
                    list->insert(0, std::move(clear));
                }
                EffectsTable::writeConsumeEffects(list, now.deathEffects);
                if (list->children.empty())
                    removeChild(d, "death_effects");
            }
        if (now.groups != was.groups || now.potionEffects != was.potionEffects || now.potionColor != was.potionColor)
            if (Tag* p = groupOn(now, QStringLiteral("minecraft:potion_contents")) ? childOf(comp, "minecraft:potion_contents")
                                                                                   : nullptr) {
                if (p->type == TagType::String) {
                    const QString potion = stringOf(p);
                    p = ensureChild(comp, TagType::Compound, "minecraft:potion_contents");
                    setString(p, "potion", potion);
                }
                removeChild(p, "custom_effects");
                if (!now.potionEffects.empty()) {
                    Tag* list = ensureChild(p, TagType::List, "custom_effects");
                    for (const EffectRow& r : now.potionEffects)
                        list->append(EffectsTable::effectTag(r));
                }
                if (now.potionColor >= 0)
                    setInt(p, TagType::Int, "custom_color", int32_t(0xff000000u | uint32_t(now.potionColor)));
                else
                    removeChild(p, "custom_color");
            }

        if (now.attributes != was.attributes) {
            Tag* old = childOf(comp, "minecraft:attribute_modifiers");
            const bool wrapped = old ? old->type == TagType::Compound : before1215;
            removeChild(comp, "minecraft:attribute_modifiers");
            if (!now.attributes.empty()) {
                Tag* list = wrapped ? ensureChild(ensureChild(comp, TagType::Compound, "minecraft:attribute_modifiers"),
                                                  TagType::List, "modifiers")
                                    : ensureChild(comp, TagType::List, "minecraft:attribute_modifiers");
                const bool prefixed = dataVersion_ >= 3837 && dataVersion_ < 4080;
                const bool uuids = dataVersion_ > 0 && dataVersion_ < 3953;
                int n = 0;
                for (const auto& [attr, amount, op, slot, original] : now.attributes) {
                    auto originals = NbtModel::deserializeTags(original);
                    std::unique_ptr<Tag> m = originals.empty() ? std::make_unique<Tag>(TagType::Compound)
                                                               : std::move(originals.front());
                    m->name.clear();
                    QString type = attr;
                    if (prefixed)
                        type = (kPlayerAttributes.contains(attr) ? QStringLiteral("player.") : QStringLiteral("generic."))
                               + attr;
                    setString(m.get(), "type", QStringLiteral("minecraft:") + type);
                    ensureChild(m.get(), TagType::Double, "amount")->floating = amount;
                    setString(m.get(), "operation", op);
                    setString(m.get(), "slot", slot);
                    if (!m->child("id") && !m->child("uuid")) {
                        if (uuids) {
                            Tag* uuid = ensureChild(m.get(), TagType::IntArray, "uuid");
                            for (int i = 0; i < 4; ++i)
                                uuid->ints.push_back(int32_t(QRandomGenerator::global()->generate()));
                            setString(m.get(), "name", QStringLiteral("JustNBT"));
                        } else {
                            setString(m.get(), "id", QStringLiteral("justnbt:modifier_%1_%2").arg(attr).arg(n));
                        }
                    }
                    ++n;
                    list->append(std::move(m));
                }
            }
        }
        if (now.glint != was.glint) {
            if (now.glint < 0)
                removeChild(comp, "minecraft:enchantment_glint_override");
            else
                setInt(comp, TagType::Byte, "minecraft:enchantment_glint_override", now.glint);
        }
        if (now.fireproof != was.fireproof) {
            removeChild(comp, "minecraft:damage_resistant");
            removeChild(comp, "minecraft:fire_resistant");
            if (now.fireproof) {
                if (dataVersion_ > 0 && dataVersion_ < 4080) {
                    ensureChild(comp, TagType::Compound, "minecraft:fire_resistant");
                } else {
                    Tag* r = ensureChild(comp, TagType::Compound, "minecraft:damage_resistant");
                    setString(r, "types", QStringLiteral("#minecraft:is_fire"));
                }
            }
        }
        if (now.hideTooltip != was.hideTooltip) {
            removeChild(comp, "minecraft:hide_tooltip");
            if (Tag* display = childOf(comp, "minecraft:tooltip_display")) {
                removeChild(display, "hide_tooltip");
                if (display->children.empty())
                    removeChild(comp, "minecraft:tooltip_display");
            }
            if (now.hideTooltip) {
                if (before1215)
                    ensureChild(comp, TagType::Compound, "minecraft:hide_tooltip");
                else
                    setInt(ensureChild(comp, TagType::Compound, "minecraft:tooltip_display"), TagType::Byte, "hide_tooltip", 1);
            }
        }
        if (now.dyed != was.dyed) {
            Tag* old = childOf(comp, "minecraft:dyed_color");
            const bool wrapped = old ? old->type == TagType::Compound : before1215;
            removeChild(comp, "minecraft:dyed_color");
            if (now.dyed >= 0) {
                if (wrapped)
                    setInt(ensureChild(comp, TagType::Compound, "minecraft:dyed_color"), TagType::Int, "rgb", now.dyed);
                else
                    setInt(comp, TagType::Int, "minecraft:dyed_color", now.dyed);
            }
        }
    }

    for (const char* wrapper : {"components", "tag"})
        if (Tag* t = childOf(item, wrapper); t && t->type == TagType::Compound && t->children.empty())
            removeChild(item, wrapper);
    if (Tag* t = childOf(item, "tag"))
        if (Tag* d = childOf(t, "display"); d && d->children.empty())
            removeChild(t, "display");
    loaded_ = std::make_unique<Snapshot>(now);
}

void ItemEditorDialog::accept()
{
    if (!nbtTab(tabs_->currentIndex()))
        applyForms();
    QDialog::accept();
}

std::unique_ptr<Tag> ItemEditorDialog::result()
{
    return item_->clone();
}
