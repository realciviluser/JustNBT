#include "ui/Dialogs.h"

#include "ui/NbtModel.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QVBoxLayout>

#include <limits>

using nbt::Tag;
using nbt::TagType;

AddTagDialog::AddTagDialog(const Tag* container, QWidget* parent)
    : QDialog(parent), container_(container)
{
    setWindowTitle(tr("New tag"));
    auto* form = new QFormLayout;

    type_ = new QComboBox;
    for (int t = int(TagType::Byte); t <= int(TagType::LongArray); ++t)
        type_->addItem(NbtModel::typeIcon(TagType(t)), QString::fromLatin1(nbt::typeName(TagType(t))), t);
    type_->setCurrentIndex(type_->findData(int(TagType::Compound)));

    const bool fixedType = container->type == TagType::List && !container->children.empty();
    if (fixedType) {
        type_->setCurrentIndex(type_->findData(int(container->listType)));
        type_->setEnabled(false);
    }
    form->addRow(tr("Type:"), type_);

    if (container->type == TagType::Compound) {
        name_ = new QLineEdit;
        name_->setPlaceholderText(tr("for example, Health"));
        form->addRow(tr("Name:"), name_);
    } else {
        form->addRow(new QLabel(fixedType ? tr("All elements of a list have the same type.")
                                          : tr("The type of the first element sets the type of the whole list.")));
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
    if (name_)
        name_->setFocus();
}

TagType AddTagDialog::type() const
{
    return TagType(type_->currentData().toInt());
}

QString AddTagDialog::name() const
{
    return name_ ? name_->text() : QString();
}

void AddTagDialog::accept()
{
    if (name_) {
        const QString n = name_->text();
        if (n.isEmpty()) {
            QMessageBox::warning(this, windowTitle(), tr("Enter a name for the tag."));
            return;
        }
        if (container_->child(n.toStdString())) {
            QMessageBox::warning(this, windowTitle(), tr("There already is a tag named %1 here.").arg(n));
            return;
        }
    }
    QDialog::accept();
}

namespace {
template <class T>
QString joinValues(const std::vector<T>& values, int perLine)
{
    QString s;
    s.reserve(int(values.size()) * 4);
    for (size_t i = 0; i < values.size(); ++i) {
        if (i)
            s += (i % size_t(perLine) == 0) ? '\n' : ' ';
        s += QString::number(qint64(values[i]));
    }
    return s;
}

template <class T>
QString parseValues(const QString& text, std::vector<T>& out)
{
    static const QRegularExpression sep(QStringLiteral("[\\s,;]+"));
    const QStringList parts = text.split(sep, Qt::SkipEmptyParts);
    out.clear();
    out.reserve(size_t(parts.size()));
    for (int i = 0; i < parts.size(); ++i) {
        bool ok = false;
        const qlonglong v = parts[i].toLongLong(&ok);
        if (!ok || v < qlonglong(std::numeric_limits<T>::min()) || v > qlonglong(std::numeric_limits<T>::max()))
            return ArrayEditDialog::tr("Element %1 (%2) does not fit: a whole number from %3 to %4 is needed.")
                .arg(i + 1)
                .arg(parts[i])
                .arg(qlonglong(std::numeric_limits<T>::min()))
                .arg(qlonglong(std::numeric_limits<T>::max()));
        out.push_back(T(v));
    }
    return {};
}
}

ArrayEditDialog::ArrayEditDialog(const Tag* tag, QWidget* parent)
    : QDialog(parent), result_(tag->type, tag->name)
{
    setWindowTitle(QStringLiteral("%1 — %2")
                       .arg(tag->name.empty() ? tr("Array") : QString::fromStdString(tag->name),
                            QString::fromLatin1(nbt::typeName(tag->type))));
    resize(640, 420);

    info_ = new QLabel(tr("%n element(s)", nullptr, int(tag->arraySize()))
                       + tr(". Numbers separated by a space, a comma or a new line."));

    edit_ = new QPlainTextEdit;
    edit_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    switch (tag->type) {
    case TagType::ByteArray: edit_->setPlainText(joinValues(tag->bytes, 32)); break;
    case TagType::IntArray: edit_->setPlainText(joinValues(tag->ints, 16)); break;
    case TagType::LongArray: edit_->setPlainText(joinValues(tag->longs, 4)); break;
    default: break;
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(info_);
    layout->addWidget(edit_);
    layout->addWidget(buttons);
}

void ArrayEditDialog::accept()
{
    const QString text = edit_->toPlainText();
    QString error;
    switch (result_.type) {
    case TagType::ByteArray: {
        std::vector<int8_t> v;
        error = parseValues(text, v);
        if (error.isEmpty())
            result_.bytes = std::move(v);
        break;
    }
    case TagType::IntArray: {
        std::vector<int32_t> v;
        error = parseValues(text, v);
        if (error.isEmpty())
            result_.ints = std::move(v);
        break;
    }
    case TagType::LongArray: {
        std::vector<int64_t> v;
        error = parseValues(text, v);
        if (error.isEmpty())
            result_.longs = std::move(v);
        break;
    }
    default: break;
    }

    if (!error.isEmpty()) {
        QMessageBox::warning(this, windowTitle(), error);
        return;
    }
    QDialog::accept();
}
