#include "ui/InventoryPanel.h"

#include "core/GameAssets.h"
#include "core/ItemInfo.h"
#include "ui/ItemEditorDialog.h"
#include "ui/ItemVisuals.h"
#include "ui/NbtModel.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QToolButton>
#include <QContextMenuEvent>
#include <QDoubleSpinBox>
#include <QDrag>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

using nbt::Tag;
using nbt::TagType;
using namespace justnbt;

namespace {
const char* kSlotMime = "application/x-justnbt-slot";

InventoryPanel* panelOf(QObject* o)
{
    for (; o; o = o->parent())
        if (auto* p = qobject_cast<InventoryPanel*>(o))
            return p;
    return nullptr;
}

const char* const kHeart[9] = {".........", "..xx.xx..", ".xxxxxxx.", ".xxxxxxx.", "..xxxxx..",
                               "...xxx...", "....x....", ".........", "........."};
const char* const kFood[9] = {".........", ".....xx..", "....xxxx.", "...xxxxx.", "...xxxxx.",
                              "..xxxxx..", ".bxxx....", "b.b......", ".b......."};

void drawPip(QPainter& p, const QRect& r, const char* const shape[9], int fill, const QColor& color,
             bool fromRight)
{
    const qreal cell = r.width() / 9.0;
    auto inside = [&](int x, int y) {
        return x >= 0 && y >= 0 && x < 9 && y < 9 && shape[y][x] == 'x';
    };
    const QColor outline(0x20, 0x10, 0x10);
    for (int y = 0; y < 9; ++y) {
        for (int x = 0; x < 9; ++x) {
            const QRectF px(r.x() + x * cell, r.y() + y * cell, cell, cell);
            const char c = shape[y][x];
            if (c == 'b') {
                p.fillRect(px, QColor(0xe8, 0xe0, 0xd0));
            } else if (c == 'x') {
                const bool half = fromRight ? x >= 4 : x <= 4;
                const bool filled = fill == 2 || (fill == 1 && half);
                p.fillRect(px, filled ? color : QColor(0x30, 0x30, 0x30));
                if (filled && x == (fromRight ? 5 : 2) && y == 2)
                    p.fillRect(px, color.lighter(170));
            } else if (inside(x - 1, y) || inside(x + 1, y) || inside(x, y - 1) || inside(x, y + 1)) {
                p.fillRect(px, outline);
            }
        }
    }
}

QString hintSprite(SlotHint hint)
{
    switch (hint) {
    case SlotHint::Helmet: return QStringLiteral("helmet");
    case SlotHint::Chestplate: return QStringLiteral("chestplate");
    case SlotHint::Leggings: return QStringLiteral("leggings");
    case SlotHint::Boots: return QStringLiteral("boots");
    case SlotHint::Shield: return QStringLiteral("shield");
    case SlotHint::Sword: return QStringLiteral("sword");
    case SlotHint::Saddle: return QStringLiteral("saddle");
    case SlotHint::HorseArmor: return QStringLiteral("horse_armor");
    case SlotHint::LlamaArmor: return QStringLiteral("llama_armor");
    case SlotHint::None: break;
    }
    return {};
}

QString hintName(SlotHint hint)
{
    switch (hint) {
    case SlotHint::Helmet: return InventoryPanel::tr("Head");
    case SlotHint::Chestplate: return InventoryPanel::tr("Chest");
    case SlotHint::Leggings: return InventoryPanel::tr("Legs");
    case SlotHint::Boots: return InventoryPanel::tr("Feet");
    case SlotHint::Shield: return InventoryPanel::tr("Second hand");
    case SlotHint::Sword: return InventoryPanel::tr("Main hand");
    case SlotHint::Saddle: return InventoryPanel::tr("Saddle");
    case SlotHint::HorseArmor: return InventoryPanel::tr("Horse armour");
    case SlotHint::LlamaArmor: return InventoryPanel::tr("Carpet");
    case SlotHint::None: break;
    }
    return {};
}

QString effectName(const QString& id)
{
    const int colon = id.indexOf(QLatin1Char(':'));
    const QString text = GameAssets::instance().translate(
        QStringLiteral("effect.%1.%2").arg(colon >= 0 ? id.left(colon) : QStringLiteral("minecraft"), id.mid(colon + 1)));
    return text.isEmpty() ? prettifyId(id) : text;
}

QString effectPath(const QString& id)
{
    return QStringLiteral("mob_effect/") + id.mid(id.indexOf(QLatin1Char(':')) + 1);
}

QString roman(int n)
{
    static const char* const numerals[] = {"I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X"};
    return n >= 1 && n <= 10 ? QLatin1String(numerals[n - 1]) : QString::number(n);
}

QString effectTime(int ticks)
{
    if (ticks < 0)
        return QStringLiteral("∞");
    const int seconds = ticks / 20;
    return seconds >= 3600 ? QStringLiteral("%1:%2:%3").arg(seconds / 3600).arg(seconds / 60 % 60, 2, 10, QLatin1Char('0')).arg(seconds % 60, 2, 10, QLatin1Char('0'))
                           : QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

class EffectDialog : public QDialog {
public:
    EffectDialog(const Effect& effect, bool bedrock, bool modern, QWidget* parent) : QDialog(parent)
    {
        setWindowTitle(InventoryPanel::tr("Effect"));
        id_ = new QComboBox;
        const qreal dpr = devicePixelRatioF();
        QStringList ids = knownEffects(bedrock);
        if (!modern && !bedrock)
            ids = ids.mid(0, 33);
        for (const QString& id : ids)
            id_->addItem(QIcon(texturePixmap(effectPath(id), 1, dpr)), effectName(id), id);
        if (id_->findData(effect.id) < 0 && !effect.id.isEmpty())
            id_->addItem(effect.id, effect.id);
        id_->setCurrentIndex(std::max(0, id_->findData(effect.id)));
        level_ = new QSpinBox;
        level_->setRange(1, bedrock ? 128 : 256);
        level_->setValue(effect.amplifier + 1);
        seconds_ = new QSpinBox;
        seconds_->setRange(1, 100000000);
        seconds_->setSuffix(InventoryPanel::tr(" s"));
        seconds_->setValue(effect.duration > 0 ? std::max(1, effect.duration / 20) : 60);
        infinite_ = new QCheckBox(InventoryPanel::tr("Infinite"));
        infinite_->setChecked(effect.duration < 0);
        infinite_->setVisible(modern);
        connect(infinite_, &QCheckBox::toggled, seconds_, &QWidget::setDisabled);
        seconds_->setDisabled(infinite_->isChecked());
        particles_ = new QCheckBox(InventoryPanel::tr("Particles"));
        particles_->setChecked(effect.particles);
        ambient_ = new QCheckBox(InventoryPanel::tr("As from a beacon (faint particles)"));
        ambient_->setChecked(effect.ambient);

        auto* time = new QHBoxLayout;
        time->addWidget(seconds_, 1);
        time->addWidget(infinite_);
        auto* form = new QFormLayout;
        form->addRow(InventoryPanel::tr("Effect:"), id_);
        form->addRow(InventoryPanel::tr("Level:"), level_);
        form->addRow(InventoryPanel::tr("Time:"), time);
        form->addRow(QString(), particles_);
        form->addRow(QString(), ambient_);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        auto* layout = new QVBoxLayout(this);
        layout->addLayout(form);
        layout->addWidget(buttons);
    }

    Effect effect() const
    {
        Effect e;
        e.id = id_->currentData().toString();
        e.amplifier = level_->value() - 1;
        e.duration = infinite_->isChecked() && infinite_->isVisible() ? -1 : seconds_->value() * 20;
        e.particles = particles_->isChecked();
        e.ambient = ambient_->isChecked();
        return e;
    }

private:
    QComboBox* id_;
    QSpinBox* level_;
    QSpinBox* seconds_;
    QCheckBox* infinite_;
    QCheckBox* particles_;
    QCheckBox* ambient_;
};

double numberOf(const Tag* t)
{
    return nbt::isFloating(t->type) ? t->floating : double(t->integer);
}

QString number(double v)
{
    return QString::number(v, 'f', std::fmod(v, 1.0) == 0 ? 0 : 1);
}
}

class QuietEditor {
public:
    QuietEditor(NbtModel* model, const InventoryLayout& layout) : model_(model), editor_(model, layout)
    {
        model_->setQuiet(true);
    }
    ~QuietEditor() { model_->setQuiet(false); }
    InventoryEditor* operator->() { return &editor_; }

private:
    NbtModel* model_;
    InventoryEditor editor_;
};

SlotWidget::SlotWidget(int index, SlotHint hint, QWidget* parent) : QWidget(parent), index_(index), hint_(hint)
{
    setFixedSize(kSize, kSize);
    setAcceptDrops(true);
    setMouseTracking(true);
    setItem(nullptr);
}

void SlotWidget::setItem(const Tag* item)
{
    const auto stack = item ? readItemStack(*item) : std::nullopt;
    if (stack) {
        id_ = stack->id;
        count_ = stack->count;
        durability_ = stack->damageable() && stack->damage > 0
                          ? std::clamp(1.0 - double(stack->damage) / stack->maxDamage, 0.0, 1.0)
                          : -1;
        enchanted_ = !stack->enchantments.empty();
        setToolTip(describeItemLines(*stack).join(QLatin1Char('\n')));
    } else {
        id_.clear();
        count_ = 0;
        durability_ = -1;
        enchanted_ = false;
        setToolTip(hintName(hint_));
    }
    setCursor(id_.isEmpty() ? Qt::ArrowCursor : Qt::OpenHandCursor);
    update();
}

void SlotWidget::setCurrent(bool current)
{
    if (current_ != current) {
        current_ = current;
        update();
    }
}

void SlotWidget::setSelectedHotbar(bool selected)
{
    if (selectedHotbar_ != selected) {
        selectedHotbar_ = selected;
        update();
    }
}

void SlotWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const QRect r = rect();
    p.fillRect(r, QColor(0x8b, 0x8b, 0x8b));
    p.fillRect(QRect(0, 0, r.width(), 2), QColor(0x37, 0x37, 0x37));
    p.fillRect(QRect(0, 0, 2, r.height()), QColor(0x37, 0x37, 0x37));
    p.fillRect(QRect(0, r.height() - 2, r.width(), 2), Qt::white);
    p.fillRect(QRect(r.width() - 2, 0, 2, r.height()), Qt::white);

    const qreal dpr = devicePixelRatioF();
    const QRect icon(2, 2, 32, 32);
    if (!id_.isEmpty()) {
        QPixmap pm = itemPixmap(id_, 32, dpr);
        if (enchanted_) {
            QPixmap tinted = pm;
            QPainter t(&tinted);
            t.setCompositionMode(QPainter::CompositionMode_SourceAtop);
            t.fillRect(QRect(QPoint(), tinted.size() / dpr), QColor(170, 90, 255, 80));
            t.end();
            pm = tinted;
        }
        p.drawPixmap(icon, pm);
    } else if (hint_ != SlotHint::None) {
        const QPixmap pm = guiSprite(QStringLiteral("container/slot/") + hintSprite(hint_), 2, dpr);
        if (!pm.isNull())
            p.drawPixmap(icon.topLeft(), pm);
    }

    if (durability_ >= 0) {
        const QRect bar(4, r.height() - 8, 28, 4);
        p.fillRect(bar, Qt::black);
        p.fillRect(QRect(bar.x(), bar.y(), qMax(1, qRound(bar.width() * durability_)), 2),
                   QColor::fromHsvF(durability_ / 3.0, 1.0, 1.0));
    }
    if (count_ > 1) {
        QFont f = font();
        f.setBold(true);
        f.setPixelSize(13);
        p.setFont(f);
        const QRect text = r.adjusted(0, 0, -3, -1);
        p.setPen(QColor(0x3f, 0x3f, 0x3f));
        p.drawText(text.translated(1, 1), Qt::AlignRight | Qt::AlignBottom, QString::number(count_));
        p.setPen(Qt::white);
        p.drawText(text, Qt::AlignRight | Qt::AlignBottom, QString::number(count_));
    }
    if (hover_ || dropTarget_)
        p.fillRect(r.adjusted(2, 2, -2, -2), QColor(255, 255, 255, 90));
    if (selectedHotbar_) {
        p.setPen(QPen(Qt::white, 2));
        p.drawRect(QRectF(r).adjusted(1, 1, -1, -1));
    }
    if (current_) {
        p.setPen(QPen(QColor(0xff, 0xc8, 0x00), 2));
        p.drawRect(QRectF(r).adjusted(1, 1, -1, -1));
    }
}

void SlotWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        pressPos_ = event->position().toPoint();
        emit clicked(index_);
    }
}

void SlotWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (!(event->buttons() & Qt::LeftButton) || id_.isEmpty()
        || (event->position().toPoint() - pressPos_).manhattanLength() < QApplication::startDragDistance())
        return;
    auto* mime = new QMimeData;
    mime->setData(QString::fromLatin1(kSlotMime), QByteArray::number(index_));
    auto* drag = new QDrag(this);
    drag->setMimeData(mime);
    drag->setPixmap(itemPixmap(id_, 32, devicePixelRatioF()));
    drag->setHotSpot(QPoint(16, 16));
    drag->exec(Qt::MoveAction);
}

void SlotWidget::mouseReleaseEvent(QMouseEvent*) {}

void SlotWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        emit doubleClicked(index_);
}

void SlotWidget::contextMenuEvent(QContextMenuEvent* event)
{
    emit menuRequested(index_, event->globalPos());
}

void SlotWidget::dragEnterEvent(QDragEnterEvent* event)
{
    auto* source = qobject_cast<SlotWidget*>(event->source());
    if (source && source != this && event->mimeData()->hasFormat(QString::fromLatin1(kSlotMime))
        && panelOf(source) == panelOf(this)) {
        event->acceptProposedAction();
        dropTarget_ = true;
        update();
    }
}

void SlotWidget::dragLeaveEvent(QDragLeaveEvent*)
{
    dropTarget_ = false;
    update();
}

void SlotWidget::dropEvent(QDropEvent* event)
{
    dropTarget_ = false;
    update();
    const int from = event->mimeData()->data(QString::fromLatin1(kSlotMime)).toInt();
    event->acceptProposedAction();
    QTimer::singleShot(0, this, [this, from] { emit dropped(from, index_); });
}

void SlotWidget::enterEvent(QEnterEvent*)
{
    hover_ = true;
    update();
}

void SlotWidget::leaveEvent(QEvent*)
{
    hover_ = false;
    update();
}

namespace {
constexpr int kPipScale = 2;
constexpr int kPip = 9 * kPipScale;
constexpr int kStep = 8 * kPipScale;
constexpr int kRow = 10 * kPipScale;
constexpr int kPerRow = 10;
constexpr int kMaxPips = 100;
}

PipBar::PipBar(Kind kind, QWidget* parent) : QWidget(parent), kind_(kind)
{
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void PipBar::setValue(double value)
{
    value_ = value;
    update();
}

void PipBar::setMaximum(double maximum)
{
    maximum_ = std::max(1.0, maximum);
    updateGeometry();
    update();
}

int PipBar::pips() const
{
    return std::clamp(int(std::ceil(maximum_ / 2.0)), 1, kMaxPips);
}

QSize PipBar::sizeHint() const
{
    const int rows = (pips() + kPerRow - 1) / kPerRow;
    return {(kPerRow - 1) * kStep + kPip, rows * kRow - (kRow - kPip)};
}

QRect PipBar::pipRect(int i) const
{
    const int row = i / kPerRow, col = i % kPerRow;
    const int x = kind_ == Kind::Hearts ? col * kStep : sizeHint().width() - kPip - col * kStep;
    return {x, row * kRow, kPip, kPip};
}

double PipBar::valueAt(const QPoint& pos) const
{
    const int row = std::clamp(pos.y() / kRow, 0, (pips() - 1) / kPerRow);
    const int width = sizeHint().width();
    const int along = kind_ == Kind::Hearts ? pos.x() : width - 1 - pos.x();
    const int col = std::clamp(along / kStep, 0, kPerRow - 1);
    const int i = std::min(row * kPerRow + col, pips() - 1);
    const int inPip = along - col * kStep;
    double v = 2.0 * i + (inPip < kPip / 2 ? 1 : 2);
    if (kind_ == Kind::Food && v == 1 && value_ <= 1 && value_ > 0)
        v = 0;
    return std::min(v, maximum_);
}

void PipBar::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const qreal dpr = devicePixelRatioF();
    const bool hearts = kind_ == Kind::Hearts;
    const QPixmap empty = guiSprite(hearts ? QStringLiteral("hud/heart/container") : QStringLiteral("hud/food_empty"),
                                    kPipScale, dpr);
    const QPixmap full = guiSprite(hearts ? QStringLiteral("hud/heart/full") : QStringLiteral("hud/food_full"),
                                   kPipScale, dpr);
    const QPixmap half = guiSprite(hearts ? QStringLiteral("hud/heart/half") : QStringLiteral("hud/food_half"),
                                   kPipScale, dpr);
    const bool sprites = !empty.isNull() && !full.isNull() && !half.isNull();
    const int halves = int(std::ceil((preview_ >= 0 ? preview_ : value_) - 1e-6));
    for (int i = pips() - 1; i >= 0; --i) {
        const QRect r = pipRect(i);
        const int fill = halves >= 2 * i + 2 ? 2 : halves == 2 * i + 1 ? 1 : 0;
        if (sprites) {
            p.drawPixmap(r.topLeft(), empty);
            if (fill)
                p.drawPixmap(r.topLeft(), fill == 2 ? full : half);
        } else {
            drawPip(p, r, hearts ? kHeart : kFood, fill,
                    hearts ? QColor(0xd0, 0x10, 0x10) : QColor(0xb0, 0x60, 0x20), !hearts);
        }
    }
    if (preview_ >= 0) {
        p.setPen(palette().color(QPalette::Highlight));
        p.drawRect(rect().adjusted(0, 0, -1, -1));
    }
}

void PipBar::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;
    preview_ = valueAt(event->position().toPoint());
    update();
}

void PipBar::mouseMoveEvent(QMouseEvent* event)
{
    if (preview_ < 0)
        return;
    preview_ = valueAt(event->position().toPoint());
    update();
}

void PipBar::mouseReleaseEvent(QMouseEvent* event)
{
    if (preview_ < 0 || event->button() != Qt::LeftButton)
        return;
    const double chosen = preview_;
    preview_ = -1;
    update();
    emit valueChosen(chosen);
}

XpBar::XpBar(QWidget* parent) : QWidget(parent)
{
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setToolTip(InventoryPanel::tr("How far to the next level - click to set"));
}

QSize XpBar::sizeHint() const
{
    return {9 * SlotWidget::kSize, 24};
}

void XpBar::setProgress(double progress)
{
    progress_ = std::clamp(progress, 0.0, 1.0);
    update();
}

void XpBar::setLevel(int level)
{
    level_ = level;
    update();
}

void XpBar::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const qreal dpr = devicePixelRatioF();
    const QRect bar(0, height() - 10, width(), 10);
    const QPixmap back = guiSprite(QStringLiteral("hud/experience_bar_background"), 2, dpr);
    const QPixmap front = guiSprite(QStringLiteral("hud/experience_bar_progress"), 2, dpr);
    const int filled = qRound(bar.width() * progress_);
    if (!back.isNull() && !front.isNull()) {
        p.drawPixmap(bar, back);
        const QRectF source(0, 0, front.width() * progress_, front.height());
        p.drawPixmap(QRectF(bar.x(), bar.y(), filled, bar.height()), front, source);
    } else {
        p.fillRect(bar, QColor(0x20, 0x20, 0x20));
        p.fillRect(bar.adjusted(1, 1, -1, -1), QColor(0x2a, 0x3a, 0x10));
        p.fillRect(QRect(bar.x() + 1, bar.y() + 1, qMax(0, filled - 2), bar.height() - 2), QColor(0x80, 0xff, 0x20));
    }
    if (level_ > 0) {
        QFont f = font();
        f.setBold(true);
        f.setPixelSize(14);
        p.setFont(f);
        const QRect text(0, 0, width(), height() - 8);
        p.setPen(Qt::black);
        for (const QPoint d : {QPoint(1, 0), QPoint(-1, 0), QPoint(0, 1), QPoint(0, -1)})
            p.drawText(text.translated(d), Qt::AlignHCenter | Qt::AlignBottom, QString::number(level_));
        p.setPen(QColor(0x80, 0xff, 0x20));
        p.drawText(text, Qt::AlignHCenter | Qt::AlignBottom, QString::number(level_));
    }
}

void XpBar::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        emit progressChosen(std::clamp(event->position().x() / width(), 0.0, 1.0));
}

InventoryPanel::InventoryPanel(QWidget* parent) : QWidget(parent)
{
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* inner = new QWidget;
    contentLayout_ = new QVBoxLayout(inner);
    contentLayout_->setContentsMargins(10, 8, 10, 8);
    empty_ = new QLabel(tr("Select a player, a mob or a container in the tree."));
    empty_->setWordWrap(true);
    contentLayout_->addWidget(empty_);
    contentLayout_->addStretch(1);
    scroll->setWidget(inner);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(scroll);
    setMinimumWidth(9 * SlotWidget::kSize + 20 + scroll->verticalScrollBar()->sizeHint().width());
}

void InventoryPanel::setModel(NbtModel* model)
{
    if (model_)
        disconnect(model_, nullptr, this, nullptr);
    model_ = model;
    layout_ = {};
    currentTag_ = nullptr;
    if (model_) {
        connect(model_, &QAbstractItemModel::modelReset, this, &InventoryPanel::scheduleRefresh);
        connect(model_, &QAbstractItemModel::layoutChanged, this, &InventoryPanel::scheduleRefresh);
        connect(model_, &QAbstractItemModel::dataChanged, this, &InventoryPanel::scheduleRefresh);
        connect(model_, &QAbstractItemModel::rowsInserted, this, &InventoryPanel::scheduleRefresh);
        connect(model_, &QAbstractItemModel::rowsRemoved, this, &InventoryPanel::scheduleRefresh);
        connect(model_, &QAbstractItemModel::modelAboutToBeReset, this, [this] { layout_ = {}; });
        connect(model_, &QAbstractItemModel::rowsAboutToBeRemoved, this,
                [this](const QModelIndex& parent, int first, int last) {
                    if (!layout_.holder)
                        return;
                    for (int r = first; r <= last; ++r) {
                        const Tag* gone = model_->tagFromIndex(model_->index(r, 0, parent));
                        for (const Tag* t = layout_.holder; t && gone; t = t->parent)
                            if (t == gone) {
                                layout_ = {};
                                scheduleRefresh();
                                return;
                            }
                    }
                });
    }
    refresh();
}

void InventoryPanel::showHolder(Tag* holder)
{
    if (holder == layout_.holder && holder)
        return;
    layout_ = inventoryLayout(holder);
    refresh();
}

void InventoryPanel::setCurrentTag(const Tag* tag)
{
    currentTag_ = tag;
    for (size_t i = 0; i < slots_.size(); ++i) {
        const Tag* item = itemAt(layout_.holder, allSlots_[i].ref);
        bool inside = false;
        for (const Tag* t = tag; t && item && !inside; t = t->parent)
            inside = t == item;
        slots_[i]->setCurrent(inside);
    }
}

void InventoryPanel::reloadPictures()
{
    shape_.clear();
    refresh();
}

void InventoryPanel::scheduleRefresh()
{
    if (refreshPending_)
        return;
    refreshPending_ = true;
    QTimer::singleShot(0, this, [this] {
        refreshPending_ = false;
        refresh();
    });
}

const InventorySlot& InventoryPanel::slotAt(int index) const
{
    return allSlots_.at(size_t(index));
}

QString InventoryPanel::shapeKey() const
{
    if (!layout_.valid())
        return {};
    const PlayerStats s = playerStats(layout_.holder, layout_.bedrock);
    return QStringLiteral("%1|%2|%3.%4.%5.%6.%7.%8|%9|%10%11%12%13%14%15|%16.%17")
        .arg(int(layout_.kind))
        .arg(layout_.bedrock)
        .arg(layout_.armor.size())
        .arg(layout_.hands.size())
        .arg(layout_.storage.size())
        .arg(layout_.hotbar.size())
        .arg(layout_.ender.size())
        .arg(layout_.container.size())
        .arg(layout_.columns)
        .arg(s.health != nullptr)
        .arg(s.food != nullptr)
        .arg(s.xpLevel != nullptr)
        .arg(s.saturation != nullptr)
        .arg(s.gameMode != nullptr)
        .arg(title())
        .arg(layout_.mount.size())
        .arg(activeEffects(layout_.holder, layout_.bedrock).size());
}

QString InventoryPanel::title() const
{
    if (!layout_.valid())
        return {};
    if (layout_.kind == InventoryLayout::Kind::Player)
        return tr("Player");
    if (Tag* item = itemOfHolder(layout_.holder))
        if (const auto stack = readItemStack(*item))
            return itemDisplayName(*stack);
    QString id = layout_.id;
    if (id.isEmpty())
        return tr("Contents");
    if (!id.contains(QLatin1Char(':'))) {
        QString snake;
        for (int i = 0; i < id.size(); ++i) {
            if (id[i].isUpper() && i > 0)
                snake += QLatin1Char('_');
            snake += id[i].toLower();
        }
        id = QStringLiteral("minecraft:") + snake;
    }
    const int colon = id.indexOf(QLatin1Char(':'));
    const QString space = id.left(colon), path = id.mid(colon + 1);
    const GameAssets& assets = GameAssets::instance();
    for (const char* kind : {"entity", "block", "item"}) {
        const QString text = assets.translate(QStringLiteral("%1.%2.%3").arg(QLatin1String(kind), space, path));
        if (!text.isEmpty())
            return text;
    }
    return prettifyId(id);
}

void InventoryPanel::refresh()
{
    if (layout_.holder)
        layout_ = inventoryLayout(layout_.holder);
    allSlots_.clear();
    for (const auto* part : {&layout_.armor, &layout_.hands, &layout_.mount, &layout_.storage, &layout_.hotbar,
                             &layout_.ender, &layout_.container})
        allSlots_.insert(allSlots_.end(), part->begin(), part->end());
    const QString shape = shapeKey();
    if (shape != shape_) {
        shape_ = shape;
        rebuild();
        emit holderChanged(layout_.valid());
    }
    updateValues();
}

void InventoryPanel::rebuild()
{
    if (content_) {
        content_->hide();
        content_->deleteLater();
        content_ = nullptr;
    }
    slots_.clear();
    title_ = healthText_ = foodText_ = nullptr;
    hearts_ = food_ = nullptr;
    xp_ = nullptr;
    level_ = nullptr;
    saturation_ = nullptr;
    gameMode_ = nullptr;
    empty_->setVisible(!layout_.valid());
    if (!layout_.valid())
        return;

    content_ = new QWidget;
    auto* v = new QVBoxLayout(content_);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(6);

    title_ = new QLabel;
    title_->setWordWrap(true);
    QFont bold = title_->font();
    bold.setBold(true);
    bold.setPointSizeF(bold.pointSizeF() * 1.15);
    title_->setFont(bold);
    auto* titleRow = new QHBoxLayout;
    if (Tag* item = itemOfHolder(layout_.holder)) {
        if (Tag* outer = findInventoryHolder(item->parent)) {
            auto* back = new QToolButton;
            back->setText(tr("← Back"));
            back->setToolTip(tr("Back to the inventory the box lies in"));
            connect(back, &QToolButton::clicked, this, [this, outer] { emit revealTag(outer); });
            titleRow->addWidget(back);
        }
    }
    titleRow->addWidget(title_, 1);
    QString dimension;
    int px = 0, pz = 0;
    if (position(&dimension, &px, &pz)) {
        auto* map = new QToolButton;
        map->setText(tr("On the map"));
        map->setToolTip(tr("Open the map of this world at this place"));
        connect(map, &QToolButton::clicked, this, [this] {
            QString d;
            int x = 0, z = 0;
            if (position(&d, &x, &z))
                emit showOnMap(d, x, z);
        });
        titleRow->addWidget(map);
    }
    v->addLayout(titleRow);

    int index = 0;
    auto makeSlot = [&](const InventorySlot& slot) {
        auto* w = new SlotWidget(index++, slot.hint);
        connect(w, &SlotWidget::clicked, this, [this](int i) {
            if (Tag* item = itemAt(layout_.holder, slotAt(i).ref))
                emit revealTag(item);
        });
        connect(w, &SlotWidget::dropped, this, &InventoryPanel::moveItem);
        connect(w, &SlotWidget::doubleClicked, this, [this](int i) {
            Tag* item = itemAt(layout_.holder, slotAt(i).ref);
            if (Tag* inside = contentsHolder(item))
                emit revealTag(inside);
            else
                editItem(i);
        });
        connect(w, &SlotWidget::menuRequested, this, &InventoryPanel::slotMenu);
        slots_.push_back(w);
        return w;
    };
    auto grid = [&](const std::vector<InventorySlot>& part, int columns) {
        auto* g = new QGridLayout;
        g->setSpacing(0);
        g->setContentsMargins(0, 0, 0, 0);
        for (size_t i = 0; i < part.size(); ++i)
            g->addWidget(makeSlot(part[i]), int(i) / columns, int(i) % columns);
        g->setColumnStretch(columns, 1);
        return g;
    };
    auto section = [&](const QString& text) {
        auto* label = new QLabel(text);
        label->setContentsMargins(0, 6, 0, 0);
        v->addWidget(label);
    };

    if (!layout_.armor.empty() || !layout_.hands.empty() || !layout_.mount.empty()) {
        auto* row = new QHBoxLayout;
        row->setSpacing(0);
        for (const auto& s : layout_.armor)
            row->addWidget(makeSlot(s));
        if (!layout_.armor.empty())
            row->addSpacing(12);
        for (const auto& s : layout_.hands)
            row->addWidget(makeSlot(s));
        if (!layout_.hands.empty() && !layout_.mount.empty())
            row->addSpacing(12);
        for (const auto& s : layout_.mount)
            row->addWidget(makeSlot(s));
        row->addStretch(1);
        v->addLayout(row);
    }

    const PlayerStats stats = playerStats(layout_.holder, layout_.bedrock);
    auto barRow = [&](QWidget* bar, QLabel*& text) {
        auto* row = new QHBoxLayout;
        row->addWidget(bar, 0, Qt::AlignTop);
        text = new QLabel;
        row->addWidget(text, 0, Qt::AlignTop);
        row->addStretch(1);
        v->addLayout(row);
    };
    if (stats.health) {
        hearts_ = new PipBar(PipBar::Kind::Hearts);
        hearts_->setToolTip(tr("Health - click a heart for that many, its left half for a half less"));
        connect(hearts_, &PipBar::valueChosen, this, [this](double value) {
            QuietEditor(model_, layout_)->setNumber(playerStats(layout_.holder, layout_.bedrock).health, value);
        });
        barRow(hearts_, healthText_);
    }
    if (stats.food) {
        food_ = new PipBar(PipBar::Kind::Food);
        food_->setToolTip(tr("Food - it fills from the right, as in the game"));
        connect(food_, &PipBar::valueChosen, this, [this](double value) {
            QuietEditor(model_, layout_)->setNumber(playerStats(layout_.holder, layout_.bedrock).food, value);
        });
        barRow(food_, foodText_);
    }
    if (stats.xpProgress) {
        xp_ = new XpBar;
        connect(xp_, &XpBar::progressChosen, this, [this](double value) {
            QuietEditor(model_, layout_)->setNumber(playerStats(layout_.holder, layout_.bedrock).xpProgress, value);
        });
        v->addWidget(xp_);
    }
    if (stats.xpLevel || stats.saturation || stats.gameMode) {
        auto* row = new QGridLayout;
        row->setHorizontalSpacing(8);
        int r = 0;
        if (stats.xpLevel) {
            level_ = new QSpinBox;
            level_->setRange(0, 1000000);
            level_->setKeyboardTracking(false);
            connect(level_, &QSpinBox::valueChanged, this, [this](int value) {
                QuietEditor(model_, layout_)->setNumber(playerStats(layout_.holder, layout_.bedrock).xpLevel, value);
            });
            row->addWidget(new QLabel(tr("Level")), r, 0);
            row->addWidget(level_, r++, 1);
        }
        if (stats.saturation) {
            saturation_ = new QDoubleSpinBox;
            saturation_->setRange(0, 20);
            saturation_->setDecimals(1);
            saturation_->setKeyboardTracking(false);
            saturation_->setToolTip(tr("Hidden food reserve: it is used up before the food bar goes down"));
            connect(saturation_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
                QuietEditor(model_, layout_)->setNumber(playerStats(layout_.holder, layout_.bedrock).saturation,
                                                           value);
            });
            row->addWidget(new QLabel(tr("Saturation")), r, 0);
            row->addWidget(saturation_, r++, 1);
        }
        if (stats.gameMode) {
            gameMode_ = new QComboBox;
            gameMode_->addItem(tr("Survival"), 0);
            gameMode_->addItem(tr("Creative"), 1);
            gameMode_->addItem(tr("Adventure"), 2);
            if (layout_.bedrock) {
                gameMode_->addItem(tr("As the world"), 5);
                gameMode_->addItem(tr("Spectator"), 6);
            } else {
                gameMode_->addItem(tr("Spectator"), 3);
            }
            connect(gameMode_, &QComboBox::activated, this, [this](int i) {
                QuietEditor(model_, layout_)->setNumber(playerStats(layout_.holder, layout_.bedrock).gameMode,
                                                           gameMode_->itemData(i).toInt());
            });
            row->addWidget(new QLabel(tr("Game mode")), r, 0);
            row->addWidget(gameMode_, r++, 1);
        }
        row->setColumnStretch(2, 1);
        v->addLayout(row);
    }

    effectRows_.clear();
    if (layout_.kind == InventoryLayout::Kind::Player || layout_.kind == InventoryLayout::Kind::Mob) {
        auto* head = new QHBoxLayout;
        auto* label = new QLabel(tr("Effects"));
        label->setContentsMargins(0, 6, 0, 0);
        head->addWidget(label);
        head->addStretch(1);
        auto* add = new QToolButton;
        add->setText(tr("+ Add"));
        add->setToolTip(tr("Add an effect"));
        connect(add, &QToolButton::clicked, this, [this] { editEffect(-1); });
        head->addWidget(add);
        v->addLayout(head);
        const auto effects = activeEffects(layout_.holder, layout_.bedrock);
        for (int i = 0; i < int(effects.size()); ++i) {
            auto* row = new QWidget;
            row->setContextMenuPolicy(Qt::CustomContextMenu);
            row->setToolTip(tr("Double click to change, right click for more"));
            auto* h = new QHBoxLayout(row);
            h->setContentsMargins(0, 0, 0, 0);
            EffectRow er{new QLabel, new QLabel};
            er.icon->setFixedSize(36, 36);
            h->addWidget(er.icon);
            h->addWidget(er.text, 1);
            effectRows_.push_back(er);
            connect(row, &QWidget::customContextMenuRequested, this, [this, i, row](const QPoint& pos) {
                QMenu menu;
                menu.addAction(tr("Change…"), this, [this, i] { editEffect(i); });
                menu.addAction(tr("Remove the effect"), this,
                               [this, i] { QuietEditor(model_, layout_)->removeEffect(i); });
                menu.exec(row->mapToGlobal(pos));
            });
            row->installEventFilter(this);
            row->setProperty("effectIndex", i);
            v->addWidget(row);
        }
    }

    if (!layout_.storage.empty()) {
        section(tr("Inventory"));
        v->addLayout(grid(layout_.storage, 9));
    }
    if (!layout_.hotbar.empty()) {
        v->addSpacing(4);
        v->addLayout(grid(layout_.hotbar, 9));
    }
    if (!layout_.ender.empty()) {
        section(tr("Ender chest"));
        v->addLayout(grid(layout_.ender, 9));
    }
    if (!layout_.container.empty()) {
        if (layout_.kind != InventoryLayout::Kind::Container)
            section(tr("Contents"));
        v->addLayout(grid(layout_.container, layout_.columns));
    }
    auto* help = new QLabel(tr("Drag an item to another slot to move it. Right click for more."));
    help->setWordWrap(true);
    QPalette pal = help->palette();
    pal.setColor(QPalette::WindowText, pal.color(QPalette::PlaceholderText));
    help->setPalette(pal);
    help->setContentsMargins(0, 8, 0, 0);
    v->addWidget(help);

    contentLayout_->insertWidget(0, content_);
}

void InventoryPanel::updateValues()
{
    if (!layout_.valid())
        return;
    QString heading = title();
    const Tag* h = layout_.holder;
    const Tag *x = h->child("x"), *y = h->child("y"), *z = h->child("z");
    if (x && y && z && nbt::isInteger(x->type) && nbt::isInteger(y->type) && nbt::isInteger(z->type))
        heading += QStringLiteral("  (%1, %2, %3)").arg(x->integer).arg(y->integer).arg(z->integer);
    if (title_)
        title_->setText(heading);

    for (size_t i = 0; i < slots_.size(); ++i)
        slots_[i]->setItem(itemAt(layout_.holder, allSlots_[i].ref));

    const PlayerStats s = playerStats(layout_.holder, layout_.bedrock);
    if (hearts_ && s.health) {
        hearts_->setMaximum(s.maxHealth);
        hearts_->setValue(numberOf(s.health));
        healthText_->setText(QStringLiteral("%1 / %2").arg(number(numberOf(s.health)), number(s.maxHealth)));
    }
    if (food_ && s.food) {
        food_->setValue(numberOf(s.food));
        foodText_->setText(QStringLiteral("%1 / 20").arg(number(numberOf(s.food))));
    }
    if (xp_ && s.xpProgress) {
        xp_->setProgress(numberOf(s.xpProgress));
        xp_->setLevel(s.xpLevel ? int(numberOf(s.xpLevel)) : 0);
    }
    if (level_ && s.xpLevel) {
        const QSignalBlocker block(level_);
        level_->setValue(int(numberOf(s.xpLevel)));
    }
    if (saturation_ && s.saturation) {
        const QSignalBlocker block(saturation_);
        saturation_->setValue(numberOf(s.saturation));
    }
    if (gameMode_ && s.gameMode) {
        const QSignalBlocker block(gameMode_);
        const int at = gameMode_->findData(int(numberOf(s.gameMode)));
        gameMode_->setCurrentIndex(at);
    }
    const auto effects = activeEffects(layout_.holder, layout_.bedrock);
    for (size_t i = 0; i < effectRows_.size() && i < effects.size(); ++i) {
        const Effect& e = effects[i];
        const QPixmap icon = texturePixmap(effectPath(e.id), 2, devicePixelRatioF());
        effectRows_[i].icon->setPixmap(icon);
        effectRows_[i].text->setText(QStringLiteral("%1 %2 — %3").arg(effectName(e.id), roman(e.amplifier + 1),
                                                                     effectTime(e.duration)));
    }
    const int hotbarStart = this->hotbarStart();
    const int selected = s.selectedSlot ? int(numberOf(s.selectedSlot)) : -1;
    for (int i = 0; i < int(layout_.hotbar.size()); ++i)
        slots_[size_t(hotbarStart + i)]->setSelectedHotbar(i == selected);
    setCurrentTag(currentTag_);
}

void InventoryPanel::moveItem(int fromSlot, int toSlot)
{
    if (!model_ || !layout_.valid() || fromSlot < 0 || toSlot < 0 || fromSlot >= int(allSlots_.size())
        || toSlot >= int(allSlots_.size()))
        return;
    QuietEditor(model_, layout_)->move(slotAt(fromSlot).ref, slotAt(toSlot).ref);
}

void InventoryPanel::slotMenu(int index, const QPoint& globalPos)
{
    if (!model_ || !layout_.valid())
        return;
    const SlotRef ref = slotAt(index).ref;
    Tag* item = itemAt(layout_.holder, ref);
    const PlayerStats stats = playerStats(layout_.holder, layout_.bedrock);
    const int hotbarStart = this->hotbarStart();
    const bool hotbar = index >= hotbarStart && index < hotbarStart + int(layout_.hotbar.size());

    QMenu menu;
    menu.addAction(item ? tr("Edit the item…") : tr("Put an item here…"), this, [this, index] { editItem(index); });
    if (item) {
        if (Tag* inside = contentsHolder(item))
            menu.addAction(tr("Open the contents"), this, [this, inside] { emit revealTag(inside); });
        menu.addAction(tr("Show in the tree"), this, [this, item] { emit revealTag(item); });
        menu.addAction(tr("Count…"), this, [this, ref, item] {
            const auto stack = readItemStack(*item);
            const Tag* countTag = item->child("Count") ? item->child("Count") : item->child("count");
            const int limit = countTag && countTag->type == TagType::Byte ? 127 : 99;
            bool ok = false;
            const int count = QInputDialog::getInt(this, tr("Count"), stack ? itemDisplayName(*stack) : QString(),
                                                   stack ? stack->count : 1, 1, limit, 1, &ok);
            if (ok)
                QuietEditor(model_, layout_)->setCount(ref, count);
        });
    }
    if (hotbar && stats.selectedSlot) {
        menu.addAction(tr("Put in the hand"), this, [this, index, hotbarStart] {
            QuietEditor(model_, layout_)
                ->setNumber(playerStats(layout_.holder, layout_.bedrock).selectedSlot, index - hotbarStart);
        });
    }
    if (item) {
        menu.addSeparator();
        menu.addAction(tr("Remove the item"), this, [this, ref] { QuietEditor(model_, layout_)->clear(ref); });
    }
    if (!menu.isEmpty())
        menu.exec(globalPos);
}

int InventoryPanel::hotbarStart() const
{
    return int(layout_.armor.size() + layout_.hands.size() + layout_.mount.size() + layout_.storage.size());
}

void InventoryPanel::editEffect(int index)
{
    if (!model_ || !layout_.valid())
        return;
    const auto effects = activeEffects(layout_.holder, layout_.bedrock);
    Effect start;
    if (index >= 0 && index < int(effects.size()))
        start = effects[size_t(index)];
    else
        start.id = QStringLiteral("minecraft:speed"), start.duration = 60 * 20;
    const bool modern = !layout_.bedrock
                        && (layout_.holder->child("active_effects") || !layout_.holder->child("ActiveEffects"));
    EffectDialog dialog(start, layout_.bedrock, modern, this);
    if (dialog.exec() == QDialog::Accepted)
        QuietEditor(model_, layout_)->setEffect(index, dialog.effect());
}

bool InventoryPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonDblClick && watched->property("effectIndex").isValid()) {
        const int index = watched->property("effectIndex").toInt();
        QTimer::singleShot(0, this, [this, index] { editEffect(index); });
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

bool InventoryPanel::position(QString* dimension, int* x, int* z) const
{
    const Tag* h = layout_.holder;
    if (!h || itemOfHolder(layout_.holder))
        return false;
    dimension->clear();
    if (const Tag* pos = h->child("Pos"); pos && pos->type == TagType::List && pos->children.size() >= 3) {
        *x = int(std::floor(numberOf(pos->children[0].get())));
        *z = int(std::floor(numberOf(pos->children[2].get())));
    } else if (const Tag *bx = h->child("x"), *bz = h->child("z");
               bx && bz && nbt::isInteger(bx->type) && nbt::isInteger(bz->type)) {
        *x = int(bx->integer);
        *z = int(bz->integer);
    } else {
        return false;
    }
    if (const Tag* d = h->child("Dimension")) {
        if (d->type == TagType::String)
            *dimension = QString::fromStdString(d->string);
        else if (nbt::isInteger(d->type))
            *dimension = d->integer == -1 ? QStringLiteral("minecraft:the_nether")
                         : d->integer == 1 ? QStringLiteral("minecraft:the_end")
                                           : QStringLiteral("minecraft:overworld");
    } else if (const Tag* id = h->child("DimensionId"); id && nbt::isInteger(id->type)) {
        *dimension = id->integer == 1 ? QStringLiteral("minecraft:the_nether")
                     : id->integer == 2 ? QStringLiteral("minecraft:the_end")
                                        : QStringLiteral("minecraft:overworld");
    }
    return true;
}

void InventoryPanel::editItem(int index)
{
    if (!model_ || !layout_.valid() || index < 0 || index >= int(allSlots_.size()))
        return;
    const SlotRef ref = slotAt(index).ref;
    InventoryEditor editor(model_, layout_);
    Tag* item = itemAt(layout_.holder, ref);
    std::unique_ptr<Tag> start = item ? item->clone() : editor.newItem(QStringLiteral("minecraft:stone"));
    ItemEditorDialog dialog(*start, layout_.bedrock, dataVersionOf(layout_.holder), this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    auto edited = dialog.result();
    if (item) {
        auto a = item->clone(), b = edited->clone();
        a->name.clear();
        b->name.clear();
        if (NbtModel::serializeTags({a.get()}) == NbtModel::serializeTags({b.get()}))
            return;
    }
    const auto stack = readItemStack(*edited);
    QuietEditor(model_, layout_)
        ->put(ref, std::move(edited), (item ? tr("edit %1") : tr("put %1")).arg(stack ? itemDisplayName(*stack) : QString()));
}
