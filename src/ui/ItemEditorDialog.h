#pragma once

#include "core/Nbt.h"

#include <QDialog>
#include <QVariant>

#include <memory>
#include <vector>

class EffectsTable;
class NbtModel;
class QCheckBox;
class QComboBox;
class QFormLayout;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QTableWidget;
class QTreeView;
class QUndoStack;

class ItemEditorDialog : public QDialog {
    Q_OBJECT

public:
    ItemEditorDialog(const nbt::Tag& item, bool bedrock, int dataVersion, QWidget* parent = nullptr);
    ~ItemEditorDialog() override;

    std::unique_ptr<nbt::Tag> result();
    QTabWidget* tabs() const { return tabs_; }
    void addEnchantment(const QString& id, int level);
    void addConsumeEffect(const QString& id, int level, double seconds, double chance);
    void addDeathEffect(const QString& id, int level, double seconds);
    void addAttribute(const QString& attribute, double amount, const QString& operation, const QString& slot);
    int enchantmentLevelLimit() const;
    void applyForms();

    enum class Format { Bedrock, JavaLegacy, JavaComponents };
    Format format() const { return format_; }

    void accept() override;

private:
    struct Field {
        enum Kind { Int, Float, Bool, String, Flag };
        QString component;
        QString key;
        Kind kind = Int;
        QVariant absent;
        QWidget* widget = nullptr;
    };
    struct Group {
        QString component;
        QGroupBox* box = nullptr;
        QFormLayout* form = nullptr;
    };

    void buildMain();
    void buildEnchantments();
    void buildFoodTab();
    void buildPropertiesTab();
    void buildNbt();
    Group& addGroup(QWidget* page, const QString& component, const QString& title, const QString& tip = {});
    QWidget* addField(const QString& component, const QString& key, Field::Kind kind, const QString& label,
                      QFormLayout* form, QVariant absent = {}, double min = 0, double max = 1000000);
    void choice(QWidget* field, const QStringList& values);
    void loadForms();
    void rebuildNbt();
    void tabChanged(int index);
    bool nbtTab(int index) const;
    QVariant fieldValue(const Field& f) const;
    void setFieldValue(const Field& f, const QVariant& v);

    std::unique_ptr<nbt::Tag> item_;
    Format format_;
    bool bedrock_;
    int dataVersion_;
    int previousTab_ = 0;

    QTabWidget* tabs_;
    QLineEdit* id_ = nullptr;
    QSpinBox* count_ = nullptr;
    QLineEdit* name_ = nullptr;
    QLineEdit* itemName_ = nullptr;
    QPlainTextEdit* lore_ = nullptr;
    QSpinBox* durability_ = nullptr;
    QSpinBox* maxDurability_ = nullptr;
    QCheckBox* unbreakable_ = nullptr;
    QSpinBox* repairCost_ = nullptr;
    QLabel* enchantmentsNote_ = nullptr;
    QTableWidget* enchantments_ = nullptr;
    std::vector<Field> fields_;
    std::vector<Group> groups_;
    EffectsTable* consumeEffects_ = nullptr;
    EffectsTable* deathEffects_ = nullptr;
    QCheckBox* deathClears_ = nullptr;
    EffectsTable* potionEffects_ = nullptr;
    QCheckBox* potionColorOn_ = nullptr;
    QPushButton* potionColor_ = nullptr;
    QTableWidget* attributes_ = nullptr;
    QComboBox* glint_ = nullptr;
    QCheckBox* fireproof_ = nullptr;
    QCheckBox* hideTooltip_ = nullptr;
    QCheckBox* dyedOn_ = nullptr;
    QPushButton* dyed_ = nullptr;
    QTreeView* tree_ = nullptr;
    QUndoStack* treeUndo_ = nullptr;
    std::unique_ptr<NbtModel> treeModel_;

    struct Snapshot;
    std::unique_ptr<Snapshot> loaded_;
    Snapshot current() const;
};
